/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AGIS archives (".AGS", ".IB&").  A direct port of XArchive's
 * games/xagis.cpp, cross-checked against U3's AGIS handler (class jpa,
 * VMT 0054bd88).
 *
 *   there is no archive-level header; the file is a chain of members, each a
 *   31-byte header immediately followed by its payload:
 *     +0x00  4  char[4]  "AGIS"
 *     +0x04  1  u8       version, always 0x10
 *     +0x05  1  u8       method: 0 implode/binary, 1 implode/ASCII,
 *                        0xff stored
 *     +0x06  4  u32 LE   CRC32 of the plaintext
 *     +0x0a  4  u32 LE   uncompressed size
 *     +0x0e  4  u32 LE   TOTAL record size, header included
 *     +0x12  1  u8       name length, 1..12
 *     +0x13 12  char[12] name buffer
 *     +0x1f  n  bytes    payload
 *
 * Two details matter and both are load-bearing:
 *
 *   - the 12-byte name buffer is NOT NUL terminated and its tail holds stale
 *     bytes from a previously written name.  The length byte is the only
 *     authority; never scan for a terminator and never trim.
 *   - for a compressed member the payload is a complete PKWARE DCL stream
 *     including its own two-byte prelude, and the record's method byte is a
 *     verbatim copy of that stream's literal-mode byte.  Cross-checking the
 *     two, plus requiring a legal dictionary exponent of 4..6, is the
 *     strongest discriminator the format offers.
 *
 * The magic repeats on every record, not just the first, and the chain must
 * land exactly on end of file: a short tail is a reject, not an overlay.
 * Every declared record size is bounded against the real file before the
 * cursor advances.
 *
 * All 3 corpus samples in F:\ARC\ARC\AGIS parse and every member decodes to
 * its declared size with a matching CRC32.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/agis/xx_agis.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#ifdef AGIS
#define XX_AGIS_FILE_TYPE XX_FILE_TYPE_AGIS
#else
#define XX_AGIS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_AGIS_HEADER_SIZE 31
#define XX_AGIS_VERSION_10 0x10U
#define XX_AGIS_METHOD_IMPLODE_BINARY 0x00U
#define XX_AGIS_METHOD_IMPLODE_ASCII 0x01U
#define XX_AGIS_METHOD_STORED 0xffU
#define XX_AGIS_NAME_BUFFER_SIZE 12U
#define XX_AGIS_MAX_MEMBERS 100000U
/* A compressed member's plain size is not bounded by the file, so it gets its
 * own ceiling; this is the reference implementation's. */
#define XX_AGIS_MAX_PLAIN 0x10000000
/* Only 1K/2K/4K windows are legal in a DCL stream; AGIS always writes 6. */
#define XX_AGIS_DCL_MIN_DICT 4U
#define XX_AGIS_DCL_MAX_DICT 6U

typedef struct xx_agis_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_agis_member;

typedef struct xx_agis_stream_s {
    xx_agis_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_agis_stream;

static void xx_agis_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_agis_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_agis_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_agis_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_agis_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_agis_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_agis_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_agis_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_agis_stream_free(void *pointer) {
    xx_agis_stream *stream = (xx_agis_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_agis_add(xx_agis_stream *stream,
                          const xx_agis_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_agis_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_agis_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_agis_method_known(uint8_t method) {
    return method == XX_AGIS_METHOD_IMPLODE_BINARY ||
           method == XX_AGIS_METHOD_IMPLODE_ASCII ||
           method == XX_AGIS_METHOD_STORED;
}


/* --------------------------------------------------------------- parse -- */

static xx_agis_stream *xx_agis_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_agis_stream *stream = NULL;
    int64_t total;
    int64_t span;
    int64_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A record without at least one payload byte cannot exist: even a stored
     * zero-length member would be rejected by the size cross-checks below. */
    if (span < XX_AGIS_HEADER_SIZE + 1) return NULL;

    stream = (xx_agis_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    cursor = 0;
    while (cursor < span) {
        uint8_t header[XX_AGIS_HEADER_SIZE];
        xx_agis_member member;
        uint8_t method;
        uint32_t name_size;
        int64_t record_size;
        uint64_t plain;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= XX_AGIS_MAX_MEMBERS) goto fail;
        if (span - cursor < XX_AGIS_HEADER_SIZE) goto fail;
        if (!xx_agis_read_at(self, self->base_address + cursor, header,
                             sizeof(header))) {
            goto fail;
        }
        /* The magic repeats on every record; that is what makes a truncated
         * or spliced chain fail closed instead of reading as members. */
        if (xx_rt_memcmp(header, "AGIS", 4U) != 0) goto fail;
        if (header[4] != XX_AGIS_VERSION_10) goto fail;
        method = header[5];
        if (!xx_agis_method_known(method)) goto fail;

        plain = (uint64_t)xx_agis_le32(header + 10);
        record_size = (int64_t)xx_agis_le32(header + 14);
        /* The record size covers its own header; bound it against what is
         * left of the file before the cursor is moved by it. */
        if (record_size < XX_AGIS_HEADER_SIZE + 1 ||
            record_size > span - cursor || plain > XX_AGIS_MAX_PLAIN) {
            goto fail;
        }

        /* The name buffer is not NUL terminated and its tail is stale; the
         * length byte is the only authority. */
        name_size = header[18];
        if (name_size < 1U || name_size > XX_AGIS_NAME_BUFFER_SIZE) goto fail;
        {
            uint32_t index;
            for (index = 0U; index < name_size; ++index) {
                uint8_t c = header[19 + index];
                if (c < 0x20U || c > 0x7eU) goto fail;
            }
        }

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = self->base_address + cursor;
        member.header_size = XX_AGIS_HEADER_SIZE;
        member.data_offset = self->base_address + cursor + XX_AGIS_HEADER_SIZE;
        member.packed_size = record_size - XX_AGIS_HEADER_SIZE;
        member.unpacked_size = plain;
        member.method = method;
        member.crc32 = xx_agis_le32(header + 6);
        member.has_crc = true;

        if (method == XX_AGIS_METHOD_STORED) {
            /* The stored members are the structural anchor that keeps the
             * three-value method gate honest: both size fields must agree. */
            if ((uint64_t)member.packed_size != plain) goto fail;
        } else {
            uint8_t prelude[2];
            if (member.packed_size < 3) goto fail;
            if (!xx_agis_read_at(self, member.data_offset, prelude,
                                 sizeof(prelude))) {
                goto fail;
            }
            /* The method byte is a verbatim copy of the DCL stream's own
             * literal-mode byte; cross-checking them is this format's
             * strongest discriminator. */
            if (prelude[0] != method || prelude[1] < XX_AGIS_DCL_MIN_DICT ||
                prelude[1] > XX_AGIS_DCL_MAX_DICT) {
                goto fail;
            }
        }

        member.name = xx_agis_make_name(header + 19, (size_t)name_size, true);
        if (!member.name) goto fail;
        if (!xx_agis_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        cursor += record_size;
    }

    /* There is no terminator record: the chain ends by landing exactly on
     * EOF.  Accepting a short tail would turn any prefix match into a hit. */
    if (cursor != span || stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_agis_stream_free(stream);
    return NULL;
}

/* Stored members are a bounded read; compressed ones are PKWARE DCL Implode
 * streams.  Either way the header's plain size and CRC32 are the anchors, and
 * a decode that misses them is an error rather than a result. */
static bool xx_agis_decode(Abstractformat *self, const xx_agis_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size <= 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->unpacked_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!packed) return false;
    if (!xx_agis_read_at(self, member->data_offset, packed,
                         (size_t)member->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (member->method == XX_AGIS_METHOD_STORED) {
        if ((uint64_t)member->packed_size != member->unpacked_size) {
            xx_mem_free(packed);
            return false;
        }
        plain = packed;
        written = (size_t)member->packed_size;
    } else {
        plain = (uint8_t *)xx_mem_alloc(
            member->unpacked_size != 0U ? (size_t)member->unpacked_size : 1U);
        if (!plain ||
            !xx_dcl_decode_memory(packed, (size_t)member->packed_size, plain,
                                  (size_t)member->unpacked_size, &written) ||
            written != member->unpacked_size) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        xx_mem_free(packed);
    }
    if (member->has_crc &&
        xx_crc32_calc(0U, plain, written) != member->crc32) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_agis_init(xx_agis *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_AGIS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-agis");
    xx_format_set_extension(&archive->format, "ags");
    archive->format.check_is_valid = xx_agis_check_is_valid;
    archive->format.handle_base_info = xx_agis_handle_base_info;
    archive->format.get_format_size = xx_agis_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_agis_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_agis_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_agis_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_agis_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_agis_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_agis_free_archive_records_reading;
    archive->format.destroy = xx_agis_vtable_destroy;
}

xx_agis *xx_agis_create(xx_io_device *device, int64_t base_address) {
    xx_agis *archive = (xx_agis *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_agis_init(archive, device, base_address);
    return archive;
}

void xx_agis_destroy(xx_agis *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_agis_free(xx_agis *archive) {
    if (!archive) return;
    xx_agis_destroy(archive);
    xx_mem_free(archive);
}

static void xx_agis_vtable_destroy(Abstractformat *self) {
    xx_agis_destroy((xx_agis *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_agis_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_agis_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_agis_parse(self, pd);
    if (!stream) return false;
    xx_agis_stream_free(stream);
    return true;
}

bool xx_agis_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_agis *archive = (xx_agis *)self;
    xx_agis_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_agis_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_agis_stream_free(stream);
    return true;
}

int64_t xx_agis_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_agis_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_agis *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_agis_set_record(xx_archive_record *record,
                                 const xx_agis_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_agis_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_agis_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_agis_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_agis_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_agis_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_agis_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_agis_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_agis_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_agis_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_agis_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_agis_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_agis_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_agis_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_agis_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_agis_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_agis_stream *stream;
    const xx_agis_member *member;
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
    stream = (xx_agis_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_agis_path_safe(member->name)) return false;

    path_option =
        xx_agis_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_agis_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
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
        !xx_agis_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_agis_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
