/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Beame & Whiteside BW-Connect distribution files (BWNDISK.BWF,
 * BWWTCPD.BWF, BWV220A.BWF ...).
 *
 * The format has NO magic, no member count, no central directory and no
 * terminator record. It is a flat chain of members starting at offset 0:
 *
 *   record, 22 bytes (0x16), one per member:
 *     0x00   1  u8   record tag, always 0x01
 *     0x01  13  name field; the name is the NUL-terminated string inside
 *               0x01..0x0C only. Byte 0x0D is the terminator slot the
 *               reference reader NULs before reading the field, so it can
 *               never reach a name.
 *     0x0e   4  u32 LE MS-DOS packed date/time, (date << 16) | time
 *     0x12   4  i32 LE PACKED size of the payload that follows
 *     0x16   n  payload, n = the packed size
 *
 * The next record starts at 0x16 + n, and the chain must land exactly on
 * end-of-file.
 *
 * Bytes behind the name's terminating NUL hold stale writer memory, both
 * before 0x0D and at it, so the name MUST be read by scanning to the first
 * NUL. Reading the field fixed-width at either 12 or 13 bytes is what would
 * embed junk in a name.
 *
 * The payload is a plain PKWARE DCL "implode" stream, its two-byte prelude
 * included. The container never stores the plaintext length: it only falls
 * out of the DCL end-of-stream code, so this reader recovers it with
 * xx_dcl_scan_memory() while parsing. The scan doubles as the format's
 * strongest gate, because the declared packed size is exactly the bitstream
 * boundary the decoder stops at on every member of the reference corpus.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bwf/xx_bwf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_BWF_COPY_CHUNK (64 * 1024)

typedef struct xx_bwf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_bwf_member;

typedef struct xx_bwf_stream_s {
    xx_bwf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_bwf_stream;

static void xx_bwf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_bwf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_bwf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_bwf_path_safe(const char *name) {
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

static void xx_bwf_stream_free(void *pointer) {
    xx_bwf_stream *stream = (xx_bwf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_bwf_add(xx_bwf_stream *stream,
                          const xx_bwf_member *member) {
    xx_bwf_member *grown = (xx_bwf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_BWF_MIN_PACKED_SIZE 3
#define XX_BWF_MIN_ARCHIVE_SIZE (XX_BWF_RECORD_SIZE + XX_BWF_MIN_PACKED_SIZE)
#define XX_BWF_DCL_MAX_LITERAL_MODE 1U
#define XX_BWF_DCL_MIN_DICT_BITS 4U
#define XX_BWF_DCL_MAX_DICT_BITS 6U
#define XX_BWF_MAX_MEMBERS 100000
#define XX_BWF_RECORD_SIZE 22
#define XX_BWF_NAME_OFFSET 1
#define XX_BWF_MAX_NAME_SIZE 12
#define XX_BWF_MAX_STEM_SIZE 8
#define XX_BWF_MAX_EXT_SIZE 3
#define XX_BWF_DATETIME_OFFSET 0x0e
#define XX_BWF_PACKEDSIZE_OFFSET 0x12
#define XX_BWF_RECORD_TAG 0x01U
#define XX_BWF_METHOD_DCL 1U
#define XX_BWF_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_bwf_le32(const uint8_t *data);
static bool xx_bwf_name_character(uint8_t character);
static bool xx_bwf_name_field(const uint8_t *field, char **out_name);
static xx_bwf_stream *xx_bwf_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_bwf_decode(Abstractformat *self, const xx_bwf_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* A DCL stream cannot be shorter than its two prelude bytes plus one byte
 * holding the start of the end-of-stream code. */
/* PKWARE DCL prelude: literal mode (0 binary / 1 Huffman) then dictionary
 * bits, of which only 4..6 (1K/2K/4K) are legal. */
/* No member count is stored, so this is a runaway guard, not a format limit.
 * The largest reference archive holds a few hundred members. */

static uint32_t xx_bwf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The DOS 8.3 character set this format actually uses. '&' is in it because
 * the corpus really does ship AT&T.COM. Path separators and spaces are
 * excluded on purpose: the format has no directories, so a name carrying one
 * is a mis-parse rather than a subfolder. */
static bool xx_bwf_name_character(uint8_t character) {
    if (character >= 'A' && character <= 'Z') return true;
    if (character >= 'a' && character <= 'z') return true;
    if (character >= '0' && character <= '9') return true;
    switch (character) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '-':
        case '@':
        case '^':
        case '_':
        case '`':
        case '{':
        case '}':
        case '~': return true;
        default: return false;
    }
}

/* With no magic anywhere in the container, the 8.3 shape of every name is
 * half of what makes this format safe to claim; the other half is the chain
 * landing on EOF. Both are the checks a later reader will be tempted to
 * loosen, and loosening either makes BWF match arbitrary binary data. */
static bool xx_bwf_name_field(const uint8_t *field, char **out_name) {
    char buffer[XX_BWF_MAX_NAME_SIZE + 1];
    size_t length = 0U;
    size_t index;
    size_t stem_length = 0U;
    size_t extension_length = 0U;
    uint8_t character;
    bool has_dot = false;

    *out_name = NULL;
    /* Stop at the first NUL. Stale bytes follow the terminator at any
     * position - roughly 40% of reference records carry one inside the
     * 12-byte window - so a fixed-width read is what would embed junk. */
    while (length < (size_t)XX_BWF_MAX_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    if (length == 0U) return false;

    for (index = 0U; index < length; ++index) {
        character = field[index];
        if (character == '.') {
            /* A leading dot, or a second one, cannot occur in an 8.3 name. */
            if (has_dot || index == 0U) return false;
            has_dot = true;
            continue;
        }
        if (!xx_bwf_name_character(character)) return false;
        if (has_dot) {
            ++extension_length;
        } else {
            ++stem_length;
        }
    }
    if (stem_length < 1U || stem_length > (size_t)XX_BWF_MAX_STEM_SIZE) {
        return false;
    }
    if (extension_length > (size_t)XX_BWF_MAX_EXT_SIZE) return false;
    /* A trailing dot with nothing behind it is not a name. */
    if (has_dot && extension_length == 0U) return false;

    for (index = 0U; index < length; ++index) {
        buffer[index] = (char)field[index];
    }
    buffer[length] = '\0';

    *out_name = xx_str_dup(buffer);
    return *out_name != NULL;
}

static xx_bwf_stream *xx_bwf_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_bwf_stream *stream;
    uint8_t record[XX_BWF_RECORD_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;
    bool closed_on_eof = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BWF_MIN_ARCHIVE_SIZE) return NULL;

    stream = (xx_bwf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    while (!closed_on_eof) {
        xx_bwf_member member;
        uint8_t *payload;
        char *name;
        int64_t compressed_size;
        int64_t data_offset;
        size_t consumed = 0U;
        size_t produced = 0U;
        bool measured;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_BWF_MAX_MEMBERS) goto fail;
        if (!xx_bwf_range_within(span, offset, XX_BWF_RECORD_SIZE)) goto fail;
        if (!xx_bwf_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }

        /* Byte 0 is a tag, not a version: every member of every reference
         * file has it, and it is the only fixed byte the format owns. */
        if (record[0] != (uint8_t)XX_BWF_RECORD_TAG) goto fail;

        /* Signed on purpose: a packed size with the top bit set is a corrupt
         * field, not a four-gigabyte member. */
        compressed_size =
            (int64_t)(int32_t)xx_bwf_le32(record + XX_BWF_PACKEDSIZE_OFFSET);
        if (compressed_size < XX_BWF_MIN_PACKED_SIZE) goto fail;
        if (compressed_size > XX_BWF_MAX_DECODED) goto fail;
        data_offset = offset + XX_BWF_RECORD_SIZE;
        /* A member extending past EOF is a rejection, not a short read. */
        if (!xx_bwf_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
        if (!payload) goto fail;
        if (!xx_bwf_read_at(self, self->base_address + data_offset, payload,
                            (size_t)compressed_size)) {
            xx_mem_free(payload);
            goto fail;
        }
        /* The prelude check is cheap and rejects most non-BWF data before the
         * scan below has to run at all. */
        if (payload[0] > XX_BWF_DCL_MAX_LITERAL_MODE ||
            payload[1] < XX_BWF_DCL_MIN_DICT_BITS ||
            payload[1] > XX_BWF_DCL_MAX_DICT_BITS) {
            xx_mem_free(payload);
            goto fail;
        }
        /* The container stores no plaintext length anywhere, so the only way
         * to learn one is to measure the stream. */
        measured = xx_dcl_scan_memory(payload, (size_t)compressed_size,
                                      (size_t)XX_BWF_MAX_DECODED, &consumed,
                                      &produced);
        xx_mem_free(payload);
        if (!measured) goto fail;
        /* VERIFIED over the whole reference corpus: the record's packed size
         * is exactly the bitstream boundary the decoder stops at. With no
         * magic in the container, this equality is what proves the record
         * chain and the payloads describe the same file; a reader that
         * relaxes it to "consumed <= compressed_size" would start claiming
         * unrelated data. */
        if (consumed != (size_t)compressed_size) goto fail;
        /* A zero-length plaintext would make extraction write an empty file
         * and call it success. */
        if (produced == 0U || (int64_t)produced > XX_BWF_MAX_DECODED) {
            goto fail;
        }

        if (!xx_bwf_name_field(record + XX_BWF_NAME_OFFSET, &name)) goto fail;
        if (!xx_bwf_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_BWF_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = (int64_t)produced;
        member.method = XX_BWF_METHOD_DCL;
        /* Already stored as (date << 16) | time, so it is published as read. */
        member.timestamp = (uint64_t)xx_bwf_le32(record +
                                                 XX_BWF_DATETIME_OFFSET);
        /* The format has no directory entries at all. */
        member.is_folder = false;

        if (!xx_bwf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset = data_offset + compressed_size;

        /* There is no terminator record, no count and no global size field:
         * the chain ends by landing exactly on EOF. Slack is a reject rather
         * than an overlay, because with no magic that exact tiling IS the
         * signature. */
        if (offset == span) closed_on_eof = true;
    }

    if (!closed_on_eof || stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_bwf_stream_free(stream);
    return NULL;
}


/* The field is 13 bytes wide, but byte +0x0D is the terminator slot the
 * reference reader overwrites with NUL, so only 12 bytes can reach a name. */

/* The container carries no method field at all - every payload is a DCL
 * stream - so parse stamps this one synthetic value and decode refuses
 * anything else. Treating an unrecognised value as stored would hand the
 * caller garbage that looks exactly like data. */

/* The plaintext length is driven by the bitstream rather than by any stored
 * field, so both the scan in parse and the allocation here must stay
 * bounded. */

/* Every member is a complete PKWARE DCL stream whose plaintext length parse
 * already recovered with xx_dcl_scan_memory(). */
static bool xx_bwf_decode(Abstractformat *self, const xx_bwf_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_BWF_METHOD_DCL) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    /* Both numbers came out of the file; refuse rather than attempt the
     * allocation. */
    if (member->compressed_size > XX_BWF_MAX_DECODED ||
        member->uncompressed_size > XX_BWF_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_bwf_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* Exactly the measured plaintext length, or nothing: a short decode
     * reported as success is the one failure the caller cannot detect. */
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_bwf_init(xx_bwf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_BWF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bwf");
    xx_format_set_extension(&archive->format, "bwf");
    archive->format.check_is_valid = xx_bwf_check_is_valid;
    archive->format.handle_base_info = xx_bwf_handle_base_info;
    archive->format.get_format_size = xx_bwf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bwf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bwf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bwf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bwf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bwf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bwf_free_archive_records_reading;
    archive->format.destroy = xx_bwf_vtable_destroy;
}

xx_bwf *xx_bwf_create(xx_io_device *device, int64_t base_address) {
    xx_bwf *archive = (xx_bwf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bwf_init(archive, device, base_address);
    return archive;
}

void xx_bwf_destroy(xx_bwf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_bwf_free(xx_bwf *archive) {
    if (!archive) return;
    xx_bwf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bwf_vtable_destroy(Abstractformat *self) {
    xx_bwf_destroy((xx_bwf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_bwf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bwf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_bwf_parse(self, pd);
    if (!stream) return false;
    xx_bwf_stream_free(stream);
    return true;
}

bool xx_bwf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bwf *archive = (xx_bwf *)self;
    xx_bwf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_bwf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_bwf_stream_free(stream);
    return true;
}

int64_t xx_bwf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bwf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_bwf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_bwf_set_record(xx_archive_record *record,
                                 const xx_bwf_member *member) {
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

static bool xx_bwf_copy_options(xx_list_s *target,
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

static const xx_var *xx_bwf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_bwf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_bwf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_bwf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bwf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_bwf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_bwf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_bwf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_bwf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bwf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bwf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bwf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_bwf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bwf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bwf_stream *stream;
    const xx_bwf_member *member;
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
    stream = (xx_bwf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bwf_path_safe(member->name)) return false;

    path_option = xx_bwf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_bwf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_bwf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_bwf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
