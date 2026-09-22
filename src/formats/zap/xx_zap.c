/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZAP archives.
 *
 *   one 0x15 byte header per member, each followed by its data:
 *     0x00  u8  name length, 1..12
 *     0x01  name, 12 byte field; only the first <name length> bytes are name
 *     0x0d  u16 LE DOS time
 *     0x0f  u16 LE DOS date
 *     0x11  i32 LE compressed size
 *     0x15  the compressed bytes; the next header follows at 0x15 + size
 *
 * There is no magic, no member count, no central directory and no end marker:
 * the chain simply runs until a header stops making sense, and whatever is
 * left over is overlay rather than a parse error.
 *
 * There is also no uncompressed size anywhere in the container. The only way
 * to learn a member's plaintext length is to run the decoder, which is what
 * xx_dcl_scan_memory is for.
 *
 * So the whole false-positive defence has to be built from the payload. Three
 * things must hold for the first member, and the third is the decisive one:
 *
 *   the name length byte is 1..12 and the bytes it covers are printable;
 *   the two bytes at 0x15 are a DCL prologue (literal mode 0/1, dictionary
 *   4..6 bits), which is the closest thing the format has to a signature;
 *   and the stream at 0x15 actually decodes to a DCL end marker.
 *
 * The first two together are only about three bytes of evidence and will hit
 * by accident in a large sweep. The trial decode is what makes a match mean
 * something, and it is the check a later reader will be tempted to drop for
 * speed. Do not: without it this format matches nearly anything.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zap/xx_zap.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include <stdio.h>

#define XX_ZAP_COPY_CHUNK (64 * 1024)

typedef struct xx_zap_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zap_member;

typedef struct xx_zap_stream_s {
    xx_zap_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zap_stream;

static void xx_zap_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zap_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_zap_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zap_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_zap_stream_free(void *pointer) {
    xx_zap_stream *stream = (xx_zap_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zap_add(xx_zap_stream *stream,
                          const xx_zap_member *member) {
    xx_zap_member *grown = (xx_zap_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZAP_HEADER_SIZE 0x15
#define XX_ZAP_NAME_FIELD_SIZE 12
#define XX_ZAP_MIN_STREAM_SIZE 3
#define XX_ZAP_MAX_MEMBERS 100000
#define XX_ZAP_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_ZAP_MAX_LITERAL_MODE 1U
#define XX_ZAP_MIN_DICTIONARY_BITS 4U
#define XX_ZAP_MAX_DICTIONARY_BITS 6U
#define XX_ZAP_METHOD_DCL_IMPLODE 0U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static char *xx_zap_copy_name(const uint8_t *field, size_t length);
static xx_zap_stream *xx_zap_parse(Abstractformat *self, xx_pd_struct *pd);
static uint16_t xx_zap_le16(const uint8_t *data);
static uint32_t xx_zap_le32(const uint8_t *data);
static bool xx_zap_is_dcl_prologue(uint8_t literal_mode, uint8_t dictionary_bits);
static uint8_t *xx_zap_load(Abstractformat *self, int64_t offset, int64_t size, xx_pd_struct *pd);
static bool xx_zap_measure(Abstractformat *self, int64_t data_offset, int64_t compressed, int64_t *produced, xx_pd_struct *pd);
static bool xx_zap_decode(Abstractformat *self, const xx_zap_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Copy the used part of the 12 byte name field. DOS-era names are ASCII, so
 * anything outside 0x20..0x7E means this was not a header. */
static char *xx_zap_copy_name(const uint8_t *field, size_t length) {
    char *name;
    size_t index;

    if (length == 0U || length > (size_t)XX_ZAP_NAME_FIELD_SIZE) return NULL;
    for (index = 0U; index < length; ++index) {
        if (field[index] < 0x20U || field[index] > 0x7EU) return NULL;
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        name[index] = (char)field[index];
    }
    name[length] = '\0';
    return name;
}

static xx_zap_stream *xx_zap_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zap_stream *stream;
    uint8_t header[XX_ZAP_HEADER_SIZE + 2];
    int64_t total;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZAP_HEADER_SIZE + XX_ZAP_MIN_STREAM_SIZE) return NULL;

    stream = (xx_zap_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    /* The chain has no end marker, so the loop stops when a header stops
     * making sense. Everything after that point becomes overlay -- but the
     * FIRST member has to parse, or this is not a ZAP file at all. */
    while (xx_zap_range_within(span, offset,
                               XX_ZAP_HEADER_SIZE + XX_ZAP_MIN_STREAM_SIZE)) {
        xx_zap_member member;
        char *name;
        size_t name_length;
        int64_t compressed;
        int64_t data_offset;
        int64_t produced;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= XX_ZAP_MAX_MEMBERS) break;
        /* Two bytes past the header: the DCL prologue comes in the same read
         * as the fields it has to agree with. */
        if (!xx_zap_read_at(self, self->base_address + offset, header,
                            sizeof(header))) {
            goto fail;
        }

        name_length = (size_t)header[0];
        if (name_length < 1U || name_length > (size_t)XX_ZAP_NAME_FIELD_SIZE) {
            break;
        }
        /* Signed: a size with the top bit set is a corrupt field, not a two
         * gigabyte member. */
        compressed = (int64_t)(int32_t)xx_zap_le32(header + 0x11);
        if (compressed < XX_ZAP_MIN_STREAM_SIZE) break;
        data_offset = offset + XX_ZAP_HEADER_SIZE;
        /* A member running past EOF ends the chain; it is never a short
         * read to be tolerated. */
        if (!xx_zap_range_within(span, data_offset, compressed)) break;
        /* The only signature-like bytes the format has. */
        if (!xx_zap_is_dcl_prologue(header[XX_ZAP_HEADER_SIZE],
                                    header[XX_ZAP_HEADER_SIZE + 1])) {
            break;
        }

        name = xx_zap_copy_name(header + 1, name_length);
        if (!name) break;
        if (!xx_zap_path_safe(name)) {
            xx_str_free(name);
            break;
        }

        produced = 0;
        if (!xx_zap_measure(self, self->base_address + data_offset, compressed,
                            &produced, pd)) {
            /* The decisive false-positive test, and the one that is expensive
             * enough to be tempting to remove. Three bytes of header evidence
             * are not enough on their own: the first member must really
             * decode, or nothing here is a ZAP archive. */
            if (stream->count == 0U) {
                xx_str_free(name);
                goto fail;
            }
            /* A later member that will not decode leaves its length unknown
             * rather than discarding the members already recovered; decode
             * measures it again at extraction time and fails there. */
            produced = 0;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_ZAP_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed;
        /* 0 means "not recoverable"; the container has no such field. */
        member.uncompressed_size = produced;
        member.method = XX_ZAP_METHOD_DCL_IMPLODE;
        /* The raw DOS stamp, time in the low half and date in the high. */
        member.timestamp = (uint64_t)xx_zap_le16(header + 0x0d) |
                           ((uint64_t)xx_zap_le16(header + 0x0f) << 16);
        if (!xx_zap_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed;
    }

    if (stream->count == 0U) goto fail;
    /* The archive ends where the chain stopped; any tail is overlay. */
    stream->archive_size = (offset < span) ? offset : span;
    return stream;

fail:
    xx_zap_stream_free(stream);
    return NULL;
}


/* A DCL stream is at least its two prologue bytes plus one coded symbol. */
/* ZAP stores no method field: every member is a PKWARE DCL implode stream.
 * This is the reader's name for that single method, not a container value,
 * and the decode switch below refuses anything else so that a future variant
 * cannot be silently treated as stored. */

static uint16_t xx_zap_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_zap_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The two bytes a DCL stream opens with. With no magic in the container this
 * is the only header-level evidence there is, so it is checked for every
 * member and not just the first. */
static bool xx_zap_is_dcl_prologue(uint8_t literal_mode,
                                   uint8_t dictionary_bits) {
    return literal_mode <= XX_ZAP_MAX_LITERAL_MODE &&
           dictionary_bits >= XX_ZAP_MIN_DICTIONARY_BITS &&
           dictionary_bits <= XX_ZAP_MAX_DICTIONARY_BITS;
}

/* Pull a member's compressed bytes into a fresh block; caller frees. */
static uint8_t *xx_zap_load(Abstractformat *self, int64_t offset, int64_t size,
                            xx_pd_struct *pd) {
    uint8_t *packed;

    if (size <= 0 || size > XX_ZAP_MAX_DECODED ||
        (uint64_t)size > (uint64_t)SIZE_MAX || (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed) return NULL;
    if (!xx_zap_read_at(self, offset, packed, (size_t)size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* Recover a member's plaintext length by decoding it into a sliding window.
 * The container carries no such field, so this is the only source for it --
 * and, for the first member, it doubles as the format's validity test. */
static bool xx_zap_measure(Abstractformat *self, int64_t data_offset,
                           int64_t compressed, int64_t *produced,
                           xx_pd_struct *pd) {
    uint8_t *packed;
    size_t decoded = 0U;
    bool result;

    *produced = 0;
    packed = xx_zap_load(self, data_offset, compressed, pd);
    if (!packed) return false;
    result = xx_dcl_scan_memory(packed, (size_t)compressed,
                                (size_t)XX_ZAP_MAX_DECODED, NULL, &decoded) &&
             decoded != 0U;
    xx_mem_free(packed);
    if (!result || (pd && xx_pd_is_stopped(pd))) return false;
    *produced = (int64_t)decoded;
    return true;
}

/* Every member is DCL implode; there is no stored path. */
static bool xx_zap_decode(Abstractformat *self, const xx_zap_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain;
    int64_t expected;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* An unimplemented method must fail loudly: handing back the compressed
     * bytes as if they were plaintext produces garbage nobody can detect. */
    if (member->method != XX_ZAP_METHOD_DCL_IMPLODE) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    expected = member->uncompressed_size;
    if (expected == 0) {
        /* parse could not measure this member, so measure it now rather than
         * guessing a buffer size. */
        if (!xx_zap_measure(self, member->data_offset,
                            member->compressed_size, &expected, pd)) {
            return false;
        }
    }
    if (expected <= 0 || expected > XX_ZAP_MAX_DECODED ||
        (uint64_t)expected > (uint64_t)SIZE_MAX) {
        return false;
    }

    packed = xx_zap_load(self, member->data_offset, member->compressed_size,
                         pd);
    if (!packed) return false;
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc((size_t)expected);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* xx_dcl_decode_memory returns true only when the stream filled the
     * buffer exactly, so a member that decodes short can never be reported
     * as a success. */
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)expected, &written) ||
        written != (size_t)expected) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)expected;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zap_init(xx_zap *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZAP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zap");
    xx_format_set_extension(&archive->format, "zap");
    archive->format.check_is_valid = xx_zap_check_is_valid;
    archive->format.handle_base_info = xx_zap_handle_base_info;
    archive->format.get_format_size = xx_zap_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zap_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zap_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zap_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zap_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zap_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zap_free_archive_records_reading;
    archive->format.destroy = xx_zap_vtable_destroy;
}

xx_zap *xx_zap_create(xx_io_device *device, int64_t base_address) {
    xx_zap *archive = (xx_zap *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zap_init(archive, device, base_address);
    return archive;
}

void xx_zap_destroy(xx_zap *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zap_free(xx_zap *archive) {
    if (!archive) return;
    xx_zap_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zap_vtable_destroy(Abstractformat *self) {
    xx_zap_destroy((xx_zap *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zap_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zap_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zap_parse(self, pd);
    if (!stream) return false;
    xx_zap_stream_free(stream);
    return true;
}

bool xx_zap_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zap *archive = (xx_zap *)self;
    xx_zap_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zap_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zap_stream_free(stream);
    return true;
}

int64_t xx_zap_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zap_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zap *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zap_set_record(xx_archive_record *record,
                                 const xx_zap_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_zap_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_zap_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_zap_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zap_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zap_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zap_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zap_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zap_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zap_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zap_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zap_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zap_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zap_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zap_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zap_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zap_stream *stream;
    const xx_zap_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zap_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zap_path_safe(member->name)) return false;

    path_option = xx_zap_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zap_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_zap_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_zap_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
