/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stork install archives (*.stk, *.hd!).
 *
 * There is no file header: the archive is nothing but member records laid
 * end to end, each record followed by its own compressed stream.
 *
 *   member record, 0x12 bytes:
 *     0x00  u8      name length, 1..12
 *     0x01  char[12] name field; only the first `name length` bytes are
 *                   meaningful, the rest is whatever was in the producer's
 *                   buffer and must be ignored
 *     0x0d  i32 LE  compressed size of the stream that follows
 *     0x11  '$'     record terminator, present in every record
 *     0x12  the compressed stream, `compressed size` bytes
 *
 * The streams are PKWARE DCL implode, header included. The record carries
 * NO uncompressed size, so this reader measures each stream with
 * xx_dcl_scan_memory and stores what that reports. A member whose stream
 * cannot be measured is still listed - Stork sets do contain damaged
 * members - but it is marked with a method this reader's decode refuses, so
 * it can never be extracted as anything.
 *
 * The format has no magic of its own. What makes claiming it safe is three
 * things together: the first member is always the Stork runtime's own
 * association table, so the file opens with the byte 0x0a followed by
 * "@ASSOC.SAV"; that first stream must actually explode, which is a real
 * trial decode rather than a byte comparison; and the record chain must tile
 * the file exactly, landing on EOF with no slack and no overshoot.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stork/xx_stork.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_STORK_COPY_CHUNK (64 * 1024)

typedef struct xx_stork_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_stork_member;

typedef struct xx_stork_stream_s {
    xx_stork_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_stork_stream;

static void xx_stork_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_stork_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_stork_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_stork_path_safe(const char *name) {
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

static void xx_stork_stream_free(void *pointer) {
    xx_stork_stream *stream = (xx_stork_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_stork_add(xx_stork_stream *stream,
                          const xx_stork_member *member) {
    xx_stork_member *grown = (xx_stork_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_STORK_RECORD_SIZE 0x12
#define XX_STORK_NAME_FIELD_SIZE 12
#define XX_STORK_TERMINATOR '$'
#define XX_STORK_ANCHOR_SIZE 11
#define XX_STORK_MIN_PAYLOAD_SIZE 3
#define XX_STORK_MAX_MEMBERS 100000
#define XX_STORK_MAX_OUTPUT (512 * 1024 * 1024)
#define XX_STORK_MAX_PACKED (256 * 1024 * 1024)
#define XX_STORK_MAX_DECODED (256 * 1024 * 1024)
#define XX_STORK_METHOD_UNKNOWN 0U
#define XX_STORK_METHOD_DCL 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_stork_le32(const uint8_t *data);
static bool xx_stork_name_valid(const uint8_t *name, size_t length);
static xx_stork_stream *xx_stork_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_stork_decode(Abstractformat *self, const xx_stork_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The smallest stream is the two DCL prelude bytes plus one coded byte. */
/* Ceiling on what one member may expand to while being measured. */
/* And on what may be read in to measure it. */

static uint32_t xx_stork_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Stork names are DOS 8.3 names. The reference extractor escapes anything the
 * host filesystem would reject as %XX rather than rejecting the record; this
 * reader refuses instead, because for a format with no magic beyond its first
 * eleven bytes the name being a plain printable name is part of what proves a
 * record is a record at all. */
static bool xx_stork_name_valid(const uint8_t *name, size_t length) {
    size_t index;

    if (length == 0U || length > (size_t)XX_STORK_NAME_FIELD_SIZE) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7EU) return false;
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':' ||
            name[index] == '*' || name[index] == '?' || name[index] == '"' ||
            name[index] == '<' || name[index] == '>' || name[index] == '|') {
            return false;
        }
    }
    return true;
}

static xx_stork_stream *xx_stork_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    /* The Stork runtime's own association table is the first member of every
     * archive: a name length of 0x0a followed by "@ASSOC.SAV". It is the only
     * fixed byte sequence the format has. */
    static const uint8_t anchor[XX_STORK_ANCHOR_SIZE] = {
        0x0a, '@', 'A', 'S', 'S', 'O', 'C', '.', 'S', 'A', 'V'};
    xx_stork_stream *stream = NULL;
    uint8_t record[XX_STORK_RECORD_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_STORK_RECORD_SIZE + XX_STORK_MIN_PAYLOAD_SIZE) return NULL;
    if (!xx_stork_read_at(self, self->base_address, record,
                          (size_t)XX_STORK_ANCHOR_SIZE)) {
        return NULL;
    }
    if (xx_rt_memcmp(record, anchor, (size_t)XX_STORK_ANCHOR_SIZE) != 0) {
        return NULL;
    }

    stream = (xx_stork_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_stork_member member;
        uint8_t *packed;
        char *name;
        int64_t compressed_size;
        int64_t data_offset;
        size_t name_length;
        size_t produced = 0U;
        size_t cursor;
        bool measured;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_STORK_MAX_MEMBERS) goto fail;
        /* A partial record at the end means the chain does not tile the
         * file, which in a headerless format is the rejection. */
        if ((span - offset) < XX_STORK_RECORD_SIZE) goto fail;
        if (!xx_stork_read_at(self, self->base_address + offset, record,
                              (size_t)XX_STORK_RECORD_SIZE)) {
            goto fail;
        }

        name_length = (size_t)record[0];
        if (!xx_stork_name_valid(record + 1, name_length)) goto fail;
        /* Every record ends with '$'. With no magic and no header, this byte
         * and the chain tiling the file are what keep an arbitrary file from
         * walking through as a member list. */
        if (record[0x11] != (uint8_t)XX_STORK_TERMINATOR) goto fail;

        compressed_size = (int64_t)(int32_t)xx_stork_le32(record + 0x0d);
        data_offset = offset + XX_STORK_RECORD_SIZE;
        if (compressed_size < XX_STORK_MIN_PAYLOAD_SIZE) goto fail;
        if (compressed_size > XX_STORK_MAX_PACKED) goto fail;
        if (!xx_stork_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        /* The record stores no uncompressed size, so the stream itself is
         * asked for one. For the first member this doubles as a trial decode:
         * a headerless format is only safe to claim once a real stream has
         * come out the other side. */
        packed = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
        if (!packed) goto fail;
        if (!xx_stork_read_at(self, self->base_address + data_offset, packed,
                              (size_t)compressed_size)) {
            xx_mem_free(packed);
            goto fail;
        }
        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(packed);
            goto fail;
        }
        measured = xx_dcl_scan_memory(packed, (size_t)compressed_size,
                                      (size_t)XX_STORK_MAX_OUTPUT, NULL,
                                      &produced);
        xx_mem_free(packed);
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!measured && stream->count == 0U) goto fail;
        /* Later members are allowed to be damaged: they are listed so the
         * archive's shape stays visible, but with a method decode refuses. */
        if (measured && produced == 0U) measured = false;

        name = (char *)xx_mem_alloc(name_length + 1U);
        if (!name) goto fail;
        for (cursor = 0U; cursor < name_length; ++cursor) {
            name[cursor] = (char)record[1 + cursor];
        }
        name[name_length] = '\0';
        /* The producer pads short names with spaces ("PKUNZIP.EXE "), so the
         * padding is trimmed before the name becomes a filename. */
        while (name_length > 0U && name[name_length - 1U] == ' ') {
            name[--name_length] = '\0';
        }
        if (name_length == 0U || !xx_stork_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_STORK_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = measured ? (int64_t)produced : 0;
        member.method = measured ? XX_STORK_METHOD_DCL
                                 : XX_STORK_METHOD_UNKNOWN;
        member.timestamp = 0U; /* the record carries no date or time */
        member.is_folder = false;
        if (!xx_stork_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    if (stream->count == 0U) goto fail;
    /* The chain has no terminator record; it must tile the file exactly. A
     * trailing byte, or a last member that overshoots, is a rejection. */
    if (offset != span) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_stork_stream_free(stream);
    return NULL;
}


/* Even a measured size is derived from attacker-controlled bytes, so the
 * allocation is capped rather than trusted. */

/* The container states no method. parse assigns DCL to a member whose stream
 * it managed to measure and UNKNOWN to one it could not; only the former can
 * be extracted. */

static bool xx_stork_decode(Abstractformat *self, const xx_stork_member *member,
                            uint8_t **out, size_t *out_size,
                            xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A member parse could not measure reaches here as UNKNOWN. Falling back
     * to a stored copy would hand the caller imploded bytes that look like
     * data, so it fails instead. */
    if (member->method != XX_STORK_METHOD_DCL) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0 ||
        member->uncompressed_size > XX_STORK_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_stork_read_at(self, member->data_offset, packed,
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
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* The size parse measured is the only length the caller will be told, so
     * a decode that produces anything else is a failure, not a short write. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_stork_init(xx_stork *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_STORK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stork");
    xx_format_set_extension(&archive->format, "stk");
    archive->format.check_is_valid = xx_stork_check_is_valid;
    archive->format.handle_base_info = xx_stork_handle_base_info;
    archive->format.get_format_size = xx_stork_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_stork_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_stork_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_stork_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_stork_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_stork_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_stork_free_archive_records_reading;
    archive->format.destroy = xx_stork_vtable_destroy;
}

xx_stork *xx_stork_create(xx_io_device *device, int64_t base_address) {
    xx_stork *archive = (xx_stork *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_stork_init(archive, device, base_address);
    return archive;
}

void xx_stork_destroy(xx_stork *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_stork_free(xx_stork *archive) {
    if (!archive) return;
    xx_stork_destroy(archive);
    xx_mem_free(archive);
}

static void xx_stork_vtable_destroy(Abstractformat *self) {
    xx_stork_destroy((xx_stork *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_stork_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_stork_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_stork_parse(self, pd);
    if (!stream) return false;
    xx_stork_stream_free(stream);
    return true;
}

bool xx_stork_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_stork *archive = (xx_stork *)self;
    xx_stork_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_stork_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_stork_stream_free(stream);
    return true;
}

int64_t xx_stork_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_stork_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_stork *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_stork_set_record(xx_archive_record *record,
                                 const xx_stork_member *member) {
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

static bool xx_stork_copy_options(xx_list_s *target,
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

static const xx_var *xx_stork_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_stork_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_stork_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_stork_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_stork_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_stork_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_stork_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_stork_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stork_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_stork_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_stork_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_stork_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_stork_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stork_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_stork_stream *stream;
    const xx_stork_member *member;
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
    stream = (xx_stork_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_stork_path_safe(member->name)) return false;

    path_option = xx_stork_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_stork_decode(self, member, &plain, &plain_size, pd);
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
        !xx_stork_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_stork_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
