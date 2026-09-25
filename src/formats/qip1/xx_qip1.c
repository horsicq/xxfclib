/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quarterdeck QIP install archives, "QD" member-stream variant
 * (QRAM.QIP, QEMM386.QIP and friends).
 *
 * There is no package index at all: the file is nothing but member records
 * laid end to end, each record being
 *
 *   0x00  u16 LE  'QD' (0x4451)
 *   0x02  u16 LE  0, in every record of the reference corpus
 *   0x04  i32 LE  compressed size of this member's stream
 *   0x08  u16 LE  member sequence number, 1 for the first record
 *   0x0a  u8      flag byte (0x21 throughout the corpus; it does NOT select
 *                 the codec, so it is carried only as a comment here)
 *   0x0b  u16 LE  MS-DOS time
 *   0x0d  u16 LE  MS-DOS date
 *   0x0f  i32 LE  uncompressed size
 *   0x13  char[13] member name, NUL padded; the last byte is forced to NUL,
 *                 so 12 characters is the real ceiling
 *   0x20  compressed stream, compressed size bytes
 *
 * The stream is a complete PKWARE Data Compression Library "implode" stream,
 * header included, so no codec is introduced here.
 *
 * The container states no method number anywhere. The reference reader
 * derives one the same way this reader does: an uncompressed size of 0 means
 * the stream is stored verbatim, anything else means DCL implode. Those two
 * derived values are what land in member.method.
 *
 * The magic is only two bytes, which on its own says nothing at all. What
 * makes detection safe is that every record states its own stream size, so
 * the record chain must tile the file exactly: record, stream, record,
 * stream, landing precisely on EOF. A partial record at the end, or a chain
 * that overshoots, is a rejection.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qip1/xx_qip1.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_QIP1_COPY_CHUNK (64 * 1024)

typedef struct xx_qip1_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_qip1_member;

typedef struct xx_qip1_stream_s {
    xx_qip1_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_qip1_stream;

static void xx_qip1_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_qip1_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_qip1_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_qip1_path_safe(const char *name) {
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

static void xx_qip1_stream_free(void *pointer) {
    xx_qip1_stream *stream = (xx_qip1_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_qip1_add(xx_qip1_stream *stream,
                          const xx_qip1_member *member) {
    xx_qip1_member *grown = (xx_qip1_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_QIP1_RECORD_SIZE 32
#define XX_QIP1_MAGIC 0x4451U /* 'Q','D' */
#define XX_QIP1_NAME_SIZE 13
#define XX_QIP1_MAX_MEMBERS 100000
#define XX_QIP1_MAX_UNCOMPRESSED 0x40000000
#define XX_QIP1_MAX_DECODED (256 * 1024 * 1024)
#define XX_QIP1_METHOD_STORE 0U
#define XX_QIP1_METHOD_DCL 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_qip1_le16(const uint8_t *data);
static uint32_t xx_qip1_le32(const uint8_t *data);
static bool xx_qip1_name_valid(const char *name);
static xx_qip1_stream *xx_qip1_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_qip1_decode(Abstractformat *self, const xx_qip1_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 1 GB sanity cap on the stated uncompressed size, as in the reference. */

static uint16_t xx_qip1_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_qip1_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS 8.3 names: printable ASCII only, and none of the characters a path
 * cannot carry. The format genuinely permits 0x20 (space) inside a name, so
 * only bytes below 0x20 and above 0x7E are refused. */
static bool xx_qip1_name_valid(const char *name) {
    size_t index;

    if (!name || !name[0]) return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        uint8_t byte = (uint8_t)name[index];

        if (byte < 0x20U || byte > 0x7EU) return false;
        if (byte == '"' || byte == '*' || byte == '<' || byte == '>' ||
            byte == '?' || byte == '|' || byte == ':' || byte == '\\' ||
            byte == '/') {
            return false;
        }
    }
    return true;
}

static xx_qip1_stream *xx_qip1_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_qip1_stream *stream = NULL;
    uint8_t record[XX_QIP1_RECORD_SIZE];
    char name[XX_QIP1_NAME_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* One record plus the smallest stream a DCL codec header can occupy. */
    if (span < XX_QIP1_RECORD_SIZE + 2) return NULL;

    stream = (xx_qip1_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_qip1_member member;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        uint16_t sequence;
        size_t index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_QIP1_MAX_MEMBERS) goto fail;
        /* A partial record at the end means the chain does not tile the
         * file, which for a container with a 2-byte magic is reason enough
         * to say no. */
        if ((span - offset) < XX_QIP1_RECORD_SIZE) goto fail;
        if (!xx_qip1_read_at(self, self->base_address + offset, record,
                             (size_t)XX_QIP1_RECORD_SIZE)) {
            goto fail;
        }

        /* Two magic bytes alone are near-worthless; it is the chain below
         * that carries the detection. */
        if (xx_qip1_le16(record) != XX_QIP1_MAGIC) goto fail;
        /* Zero in every record of the corpus, and the cheapest extra bit of
         * signal the record offers. */
        if (xx_qip1_le16(record + 2) != 0U) goto fail;

        compressed_size = (int64_t)(int32_t)xx_qip1_le32(record + 4);
        sequence = xx_qip1_le16(record + 8);
        uncompressed_size = (int64_t)(int32_t)xx_qip1_le32(record + 15);
        if (compressed_size < 0 || uncompressed_size < 0 ||
            uncompressed_size > XX_QIP1_MAX_UNCOMPRESSED) {
            goto fail;
        }
        /* Only the first record's sequence number is fixed by the format;
         * the rest simply count up, and nothing downstream depends on them.
         * The leading 1 is a real constraint on the first record, so it is
         * part of the detection. */
        if (stream->count == 0U && sequence != 1U) goto fail;
        if (sequence == 0U) goto fail;

        for (index = 0U; index < (size_t)XX_QIP1_NAME_SIZE; ++index) {
            name[index] = (char)record[19 + index];
        }
        /* The writer forces the last byte to NUL, so a name is at most 12
         * characters and is always terminated. */
        name[XX_QIP1_NAME_SIZE - 1] = '\0';
        if (!xx_qip1_name_valid(name)) goto fail;

        data_offset = offset + XX_QIP1_RECORD_SIZE;
        if (!xx_qip1_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        /* Both sizes are stated, so an empty member would mean an empty DCL
         * stream, which cannot exist: the 2-byte codec header alone is
         * bigger. */
        if (compressed_size < 2) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_QIP1_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        if (uncompressed_size == 0) {
            /* The record states no method. A zero uncompressed size is how
             * this container spells "stored", and it then states no plain
             * length either, so the stream's own length is it. */
            member.method = XX_QIP1_METHOD_STORE;
            member.uncompressed_size = compressed_size;
        } else {
            member.method = XX_QIP1_METHOD_DCL;
            member.uncompressed_size = uncompressed_size;
        }
        /* Raw MS-DOS time and date, packed time | (date << 16); there is no
         * CRC anywhere in the record. */
        member.timestamp = (uint64_t)xx_qip1_le16(record + 11) |
                           ((uint64_t)xx_qip1_le16(record + 13) << 16);
        member.is_folder = false;
        if (!xx_qip1_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    if (stream->count == 0U) goto fail;
    /* The whole defence against a false positive: the record chain must tile
     * the span exactly, landing on EOF. Without it any file whose first two
     * bytes are 'QD' is a candidate, so this must not be loosened into "the
     * chain fits inside the file". */
    if (offset != span) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_qip1_stream_free(stream);
    return NULL;
}


/* Refuse to allocate more than this for one member, whatever the record
 * claims: the uncompressed size is attacker-controlled. */

/* The container has no method field; parse derives these two from the
 * uncompressed size, so they are the only values that can appear here. */

static bool xx_qip1_decode(Abstractformat *self, const xx_qip1_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0 ||
        member->uncompressed_size > XX_QIP1_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    /* Any method but the two parse can produce would mean a reader change
     * that forgot this switch; treating it as stored would hand the caller
     * compressed bytes dressed up as data. */
    if (member->method != XX_QIP1_METHOD_STORE &&
        member->method != XX_QIP1_METHOD_DCL) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_qip1_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_QIP1_METHOD_STORE) {
        size_t index;
        /* parse sets uncompressed_size = compressed_size for a stored
         * member, so this is a straight copy of the whole stream. */
        for (index = 0U; index < (size_t)member->uncompressed_size; ++index) {
            plain[index] = packed[index];
        }
        written = (size_t)member->uncompressed_size;
    } else if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, (size_t)member->uncompressed_size,
                                     &written)) {
        written = 0U;
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* A short decode reported as success is the one failure a caller cannot
     * detect, so the decoded length must match the record exactly. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_qip1_init(xx_qip1 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_QIP1;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-qip");
    xx_format_set_extension(&archive->format, "qip");
    archive->format.check_is_valid = xx_qip1_check_is_valid;
    archive->format.handle_base_info = xx_qip1_handle_base_info;
    archive->format.get_format_size = xx_qip1_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qip1_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qip1_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qip1_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qip1_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qip1_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qip1_free_archive_records_reading;
    archive->format.destroy = xx_qip1_vtable_destroy;
}

xx_qip1 *xx_qip1_create(xx_io_device *device, int64_t base_address) {
    xx_qip1 *archive = (xx_qip1 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_qip1_init(archive, device, base_address);
    return archive;
}

void xx_qip1_destroy(xx_qip1 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_qip1_free(xx_qip1 *archive) {
    if (!archive) return;
    xx_qip1_destroy(archive);
    xx_mem_free(archive);
}

static void xx_qip1_vtable_destroy(Abstractformat *self) {
    xx_qip1_destroy((xx_qip1 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_qip1_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_qip1_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_qip1_parse(self, pd);
    if (!stream) return false;
    xx_qip1_stream_free(stream);
    return true;
}

bool xx_qip1_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_qip1 *archive = (xx_qip1 *)self;
    xx_qip1_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_qip1_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_qip1_stream_free(stream);
    return true;
}

int64_t xx_qip1_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_qip1_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_qip1 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_qip1_set_record(xx_archive_record *record,
                                 const xx_qip1_member *member) {
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

static bool xx_qip1_copy_options(xx_list_s *target,
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

static const xx_var *xx_qip1_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_qip1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_qip1_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_qip1_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_qip1_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_qip1_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_qip1_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_qip1_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_qip1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qip1_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_qip1_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_qip1_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_qip1_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qip1_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_qip1_stream *stream;
    const xx_qip1_member *member;
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
    stream = (xx_qip1_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_qip1_path_safe(member->name)) return false;

    path_option = xx_qip1_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_qip1_decode(self, member, &plain, &plain_size, pd);
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
        !xx_qip1_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_qip1_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
