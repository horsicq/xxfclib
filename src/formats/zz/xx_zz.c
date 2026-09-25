/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZZ archives.
 *
 *   header, 16 bytes, little-endian:
 *     0x00  u32  magic 0x00025A5A - the ASCII pair "ZZ" then 0x02, 0x00
 *     0x04  u32  uncompressed size, read as a SIGNED 32 bit value and
 *                required to be strictly positive: 0 and anything with the
 *                high bit set are rejected
 *     0x08  u64  reserved, MUST be zero
 *     0x10       the member's zlib stream, running to EOF
 *
 * The container holds exactly one member and no name: there is no directory,
 * no per-member record and no terminator. The reference reader takes the
 * member name from the host file name and falls back to "zz"; nothing inside
 * the file carries it, so this reader always publishes "zz".
 *
 * There is no method field either. A ZZ payload is always a zlib (RFC 1950)
 * stream - header, raw Deflate, Adler-32 - so member.method is set to the
 * fixed value 8, Deflate, which is what the reference reports; the decode
 * switch refuses every other value rather than guessing.
 *
 * A file must be at least 0x12 bytes: the header plus the two byte zlib
 * prologue, the smallest thing that could be a member at all.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zz/xx_zz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <stdio.h>

#define XX_ZZ_COPY_CHUNK (64 * 1024)

typedef struct xx_zz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zz_member;

typedef struct xx_zz_stream_s {
    xx_zz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zz_stream;

static void xx_zz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zz_path_safe(const char *name) {
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

static void xx_zz_stream_free(void *pointer) {
    xx_zz_stream *stream = (xx_zz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zz_add(xx_zz_stream *stream,
                          const xx_zz_member *member) {
    xx_zz_member *grown = (xx_zz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZZ_HEADER_SIZE 0x10
#define XX_ZZ_MIN_SIZE 0x12
#define XX_ZZ_MAGIC 0x00025A5AUL
#define XX_ZZ_MAX_MEMBERS 1
#define XX_ZZ_MAX_DECODED (256 * 1024 * 1024)
#define XX_ZZ_METHOD_DEFLATE 8U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_zz_le32(const uint8_t *data);
static xx_zz_stream *xx_zz_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zz_decode(Abstractformat *self, const xx_zz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Header plus the two byte zlib prologue - below this nothing can be a
 * member. */
/* The container holds exactly one member; the cap is here so the shape of
 * this reader matches the others and cannot silently grow a list. */

static uint32_t xx_zz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_zz_stream *xx_zz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zz_stream *stream = NULL;
    xx_zz_member member;
    uint8_t header[XX_ZZ_HEADER_SIZE + 2];
    uint32_t raw_size;
    int64_t uncompressed;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZZ_MIN_SIZE) return NULL;
    /* Two bytes past the header are read together with it: they are the zlib
     * prologue, checked below. */
    if (!xx_zz_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    if (xx_zz_le32(header) != (uint32_t)XX_ZZ_MAGIC) return NULL;

    /* The length is read as a signed 32 bit value and must be strictly
     * positive, so 0 and any value with the high bit set are rejected. An
     * empty member is not representable in this container. */
    raw_size = xx_zz_le32(header + 4);
    if (raw_size == 0U || (raw_size & 0x80000000UL) != 0U) return NULL;
    uncompressed = (int64_t)raw_size;

    /* Eight reserved bytes that must all be zero. With only a four byte magic
     * this is the bulk of the whole-file defence: "ZZ\x02\x00" alone turns up
     * inside ordinary binary data, and a later reader tempted to drop this
     * check would make every such hit a ZZ archive. */
    if (xx_zz_le32(header + 8) != 0U) return NULL;
    if (xx_zz_le32(header + 12) != 0U) return NULL;

    /* The payload is a zlib stream by definition, so its two byte prologue
     * (method/window, then the check bits and the preset-dictionary flag) is
     * a structural field of the container. It is the second half of the
     * false-positive defence: a zero-filled region that happens to carry the
     * magic will not also carry a well-formed zlib header. */
    if (!xx_zlib_stream_header_is_valid(header + XX_ZZ_HEADER_SIZE, 2U)) {
        return NULL;
    }

    if (pd && xx_pd_is_stopped(pd)) return NULL;

    stream = (xx_zz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    /* The payload runs from the header to EOF; there is no stored compressed
     * length to disagree with. */
    if (!xx_zz_range_within(span, XX_ZZ_HEADER_SIZE,
                            span - XX_ZZ_HEADER_SIZE)) {
        goto fail;
    }
    if (stream->count >= (size_t)XX_ZZ_MAX_MEMBERS) goto fail;

    /* Nothing in the file names the member; the reference derives it from the
     * host file name and falls back to "zz", which is all that is available
     * here. */
    member.name = xx_str_dup("zz");
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = XX_ZZ_HEADER_SIZE;
    member.data_offset = self->base_address + XX_ZZ_HEADER_SIZE;
    member.compressed_size = span - XX_ZZ_HEADER_SIZE;
    member.uncompressed_size = uncompressed;
    member.method = XX_ZZ_METHOD_DEFLATE;
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_zz_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    stream->archive_size = span;
    return stream;

fail:
    xx_zz_stream_free(stream);
    return NULL;
}


/* A container-supplied decoded length is attacker controlled; the header
 * field itself tops out at 0x7FFFFFFF, so without a ceiling a 16 byte file
 * could demand a 2 GiB allocation. */
/* ZZ carries no method field: the payload is a zlib stream by definition.
 * The raw value parse publishes is 8, Deflate, which is what the reference
 * reports for the member, and it is the only value decode accepts. */

static bool xx_zz_decode(Abstractformat *self, const xx_zz_member *member,
                         uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size;
    size_t output_size;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* ZZ defines no method field, so parse always stores 8 (Deflate). Any
     * other value means the member did not come from this parse; treating it
     * as stored would hand the caller a compressed blob dressed up as data. */
    if (member->method != XX_ZZ_METHOD_DEFLATE) return false;

    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->compressed_size > (int64_t)XX_ZZ_MAX_DECODED) return false;
    if (member->uncompressed_size > (int64_t)XX_ZZ_MAX_DECODED) return false;

    input_size = (size_t)member->compressed_size;
    output_size = (size_t)member->uncompressed_size;

    input = (uint8_t *)xx_mem_alloc(input_size);
    if (!input) return false;
    if (!xx_zz_read_at(self, member->data_offset, input, input_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    /* Allocate exactly the declared length and require the decoder to fill
     * it: a stream that stops short is a corrupt member, never a success. */
    if (!xx_zlib_stream_decode_memory(input, input_size, output, output_size,
                                      &written) ||
        (written != output_size)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }

    /* The member runs to EOF by construction, so the Adler-32 is inside the
     * stream and can be demanded - unlike a container that cuts the stream at
     * its last Deflate byte. */
    if (!xx_zlib_stream_trailer_matches(input, input_size, output,
                                        output_size)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }

    xx_mem_free(input);
    *out = output;
    *out_size = output_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zz_init(xx_zz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zz");
    xx_format_set_extension(&archive->format, "zz");
    archive->format.check_is_valid = xx_zz_check_is_valid;
    archive->format.handle_base_info = xx_zz_handle_base_info;
    archive->format.get_format_size = xx_zz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zz_free_archive_records_reading;
    archive->format.destroy = xx_zz_vtable_destroy;
}

xx_zz *xx_zz_create(xx_io_device *device, int64_t base_address) {
    xx_zz *archive = (xx_zz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zz_init(archive, device, base_address);
    return archive;
}

void xx_zz_destroy(xx_zz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zz_free(xx_zz *archive) {
    if (!archive) return;
    xx_zz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zz_vtable_destroy(Abstractformat *self) {
    xx_zz_destroy((xx_zz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zz_parse(self, pd);
    if (!stream) return false;
    xx_zz_stream_free(stream);
    return true;
}

bool xx_zz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zz *archive = (xx_zz *)self;
    xx_zz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zz_stream_free(stream);
    return true;
}

int64_t xx_zz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zz_set_record(xx_archive_record *record,
                                 const xx_zz_member *member) {
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

static bool xx_zz_copy_options(xx_list_s *target,
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

static const xx_var *xx_zz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zz_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zz_stream *stream;
    const xx_zz_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zz_path_safe(member->name)) return false;

    path_option = xx_zz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_zz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
