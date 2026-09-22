/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CRUSH archives.
 *
 *   header, 0x1a bytes:
 *     +0x00  "CRUSH v1"
 *     +0x08  '.'
 *     +0x09  u8 minor version
 *     +0x0a  0x0a 0x1a 0x00
 *     +0x10  u16 LE number of directories (< 0x100)
 *     +0x12  u16 LE number of members (> 1)
 *     +0x14  u16 LE, must be zero
 *     +0x16  u32 LE offset of the entry table
 *
 *   entry table, one 0x18-byte entry per member:
 *     +0x00  u8 directory index, 0 for "no directory"
 *     +0x02  u16 LE DOS time
 *     +0x04  u16 LE DOS date
 *     +0x06  u32 LE stored size
 *     +0x0a  name, 12 bytes, NUL padded only when shorter
 *
 *   directory name pool: the entry table is immediately followed by one
 *   NUL-terminated path per directory, back to back. The end of the last one
 *   is the end of the archive proper.
 *
 * There are no per-member data offsets. The payload sits between the header
 * and the table, and each member starts where the previous one ended, so a
 * single wrong size does not corrupt one member -- it shifts every member
 * after it. That is why a member that does not fit is fatal rather than
 * skippable: there is nothing left to salvage behind it.
 *
 * The banner is what carries the format: eight signature bytes plus the four
 * fixed punctuation bytes ('.', LF, SUB, NUL) and the zero u16 at 0x14. The
 * SUB is the DOS "type the archive" guard and the NUL at 0x0c terminates the
 * banner as a C string; together they are the only thing standing between
 * this reader and any file that happens to begin with the word CRUSH.
 *
 * A directory index is stored one greater than the slot it names, so that
 * zero can mean "none"; reading it as a plain index puts every member one
 * directory too far down the pool.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cru/xx_cru.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_CRU_COPY_CHUNK (64 * 1024)

typedef struct xx_cru_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_cru_member;

typedef struct xx_cru_stream_s {
    xx_cru_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cru_stream;

static void xx_cru_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cru_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_cru_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_cru_path_safe(const char *name) {
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

static void xx_cru_stream_free(void *pointer) {
    xx_cru_stream *stream = (xx_cru_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_cru_add(xx_cru_stream *stream,
                          const xx_cru_member *member) {
    xx_cru_member *grown = (xx_cru_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_cru_decode(Abstractformat *self,
                             const xx_cru_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_cru_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_CRU_HEADER_SIZE 0x1A
#define XX_CRU_ENTRY_SIZE 0x18
#define XX_CRU_NAME_SIZE 12
#define XX_CRU_MAX_MEMBERS 100000
#define XX_CRU_MAX_DIRECTORIES 0x100
#define XX_CRU_MAX_PATH_SIZE 4096

static const uint8_t XX_CRU_SIGNATURE[8] = {'C', 'R', 'U', 'S', 'H',
                                            ' ', 'v', '1'};

static uint16_t xx_cru_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_cru_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_cru_stream *xx_cru_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_cru_stream *stream = NULL;
    uint8_t *table = NULL;
    char *directories[XX_CRU_MAX_DIRECTORIES];
    uint8_t header[XX_CRU_HEADER_SIZE];
    char path[XX_CRU_MAX_PATH_SIZE + 1];
    char full[XX_CRU_MAX_PATH_SIZE + XX_CRU_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t table_offset;
    int64_t table_size;
    int64_t offset;
    int64_t data_offset;
    int64_t archive_size;
    int directory_count;
    int member_count;
    int index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CRU_HEADER_SIZE) return NULL;
    if (!xx_cru_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* The banner is the whole of the format's identity: the eight signature
     * bytes alone match any file that opens with the word CRUSH, so the four
     * fixed bytes after it -- the '.' before the minor version, and the
     * LF/SUB/NUL that close the banner for DOS TYPE -- have to be checked
     * too. Loosen this and every text file starting "CRUSH v1" is an
     * archive. */
    if (xx_rt_memcmp(header, XX_CRU_SIGNATURE, sizeof(XX_CRU_SIGNATURE)) != 0 ||
        header[8] != '.' || header[10] != 0x0AU || header[11] != 0x1AU ||
        header[12] != 0U) {
        return NULL;
    }

    directory_count = (int)xx_cru_le16(header + 0x10);
    member_count = (int)xx_cru_le16(header + 0x12);
    /* Reserved, and observed zero in every writer: the second structural
     * guard behind the banner. */
    if (xx_cru_le16(header + 0x14) != 0U) return NULL;
    if (directory_count >= XX_CRU_MAX_DIRECTORIES) return NULL;
    /* A single-member archive is not produced by this format, and rejecting
     * it costs nothing while removing a whole class of accidental match. */
    if (member_count <= 1 || member_count > XX_CRU_MAX_MEMBERS) return NULL;

    table_offset = (int64_t)(int32_t)xx_cru_le32(header + 0x16);
    /* The table cannot start inside the header it is announced from. */
    if (table_offset < XX_CRU_HEADER_SIZE) return NULL;
    table_size = (int64_t)member_count * XX_CRU_ENTRY_SIZE;
    if (!xx_cru_range_within(span, table_offset, table_size)) return NULL;

    xx_mem_zero(directories, sizeof(directories));

    stream = (xx_cru_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) goto fail;
    if (!xx_cru_read_at(self, self->base_address + table_offset, table,
                        (size_t)table_size)) {
        goto fail;
    }

    /* The directory paths follow the table with no count or length prefix of
     * their own; the only way to find the n-th is to walk the terminators. */
    offset = table_offset + table_size;
    for (index = 0; index < directory_count; ++index) {
        size_t length = 0U;
        size_t start = 0U;
        char *stored;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        for (;;) {
            uint8_t byte;

            if (offset >= span) goto fail;
            if (length >= XX_CRU_MAX_PATH_SIZE) goto fail;
            if (!xx_cru_read_at(self, self->base_address + offset, &byte,
                                1U)) {
                goto fail;
            }
            ++offset;
            if (byte == 0U) break;
            /* Stored paths are DOS 8.3 text; a control byte here means the
             * pool is not a pool and the walk is running through payload. */
            if (byte < 0x20U || byte > 0x7EU) goto fail;
            path[length++] = (char)byte;
        }
        path[length] = '\0';
        /* "X:\..." loses its drive and root separator. The reference drops
         * four characters flat rather than parsing the path, and a path of
         * exactly "X:\" therefore becomes empty. */
        if (length >= 3U && path[1] == ':') {
            start = (length >= 4U) ? 4U : length;
        }
        stored = xx_str_dup(path + start);
        if (!stored) goto fail;
        directories[index] = stored;
    }

    /* Everything up to the end of the name pool belongs to the archive; the
     * payload may still push this further down. */
    archive_size = offset;

    data_offset = XX_CRU_HEADER_SIZE;
    for (index = 0; index < member_count; ++index) {
        const uint8_t *entry = table + (int64_t)index * XX_CRU_ENTRY_SIZE;
        xx_cru_member member;
        int64_t size;
        int directory_index;
        size_t name_length = 0U;
        size_t prefix_length = 0U;
        size_t i;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        directory_index = (int)entry[0];
        size = (int64_t)(int32_t)xx_cru_le32(entry + 6);
        if (size < 0) goto fail;
        /* Sizes are the only thing positioning the payload, so a member that
         * runs past the end invalidates every member after it as well. */
        if (!xx_cru_range_within(span, data_offset, size)) goto fail;

        while (name_length < XX_CRU_NAME_SIZE &&
               entry[0x0A + name_length] != 0U) {
            ++name_length;
        }
        if (name_length == 0U) goto fail;
        for (i = 0U; i < name_length; ++i) {
            if (entry[0x0A + i] < 0x20U || entry[0x0A + i] > 0x7EU) goto fail;
        }

        /* Stored one greater than the slot, so that zero can mean "no
         * directory"; an index past the pool is treated as none, matching
         * the reference rather than rejecting the archive. */
        if (directory_index > 0 &&
            (directory_index - 1) < directory_count) {
            const char *prefix = directories[directory_index - 1];
            prefix_length = xx_rt_strlen(prefix);
            for (i = 0U; i < prefix_length; ++i) full[i] = prefix[i];
        }
        /* The stored path already carries its own trailing separator. */
        for (i = 0U; i < name_length; ++i) {
            full[prefix_length + i] = (char)entry[0x0A + i];
        }
        full[prefix_length + name_length] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(full);
        if (!member.name) goto fail;
        member.header_offset =
            self->base_address + table_offset + (int64_t)index * XX_CRU_ENTRY_SIZE;
        member.header_size = XX_CRU_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        member.timestamp = (uint64_t)xx_cru_le16(entry + 2) |
                           ((uint64_t)xx_cru_le16(entry + 4) << 16);
        if (!xx_cru_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        data_offset += size;
    }

    if (stream->count == 0U) goto fail;
    if (data_offset > archive_size) archive_size = data_offset;
    stream->archive_size = archive_size < span ? archive_size : span;

    xx_mem_free(table);
    for (index = 0; index < XX_CRU_MAX_DIRECTORIES; ++index) {
        xx_str_free(directories[index]);
    }
    return stream;

fail:
    xx_mem_free(table);
    for (index = 0; index < XX_CRU_MAX_DIRECTORIES; ++index) {
        xx_str_free(directories[index]);
    }
    xx_cru_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_cru_init(xx_cru *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CRU;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-crush");
    xx_format_set_extension(&archive->format, "cru");
    archive->format.check_is_valid = xx_cru_check_is_valid;
    archive->format.handle_base_info = xx_cru_handle_base_info;
    archive->format.get_format_size = xx_cru_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cru_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cru_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cru_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cru_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cru_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cru_free_archive_records_reading;
    archive->format.destroy = xx_cru_vtable_destroy;
}

xx_cru *xx_cru_create(xx_io_device *device, int64_t base_address) {
    xx_cru *archive = (xx_cru *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cru_init(archive, device, base_address);
    return archive;
}

void xx_cru_destroy(xx_cru *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cru_free(xx_cru *archive) {
    if (!archive) return;
    xx_cru_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cru_vtable_destroy(Abstractformat *self) {
    xx_cru_destroy((xx_cru *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cru_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cru_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cru_parse(self, pd);
    if (!stream) return false;
    xx_cru_stream_free(stream);
    return true;
}

bool xx_cru_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cru *archive = (xx_cru *)self;
    xx_cru_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_cru_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_cru_stream_free(stream);
    return true;
}

int64_t xx_cru_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cru_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_cru *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cru_set_record(xx_archive_record *record,
                                 const xx_cru_member *member) {
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

static bool xx_cru_copy_options(xx_list_s *target,
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

static const xx_var *xx_cru_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_cru_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_cru_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cru_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cru_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cru_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cru_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_cru_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cru_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cru_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_cru_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_cru_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cru_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cru_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_cru_stream *stream;
    const xx_cru_member *member;
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
    stream = (xx_cru_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cru_path_safe(member->name)) return false;

    path_option = xx_cru_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_cru_decode(self, member, &plain, &plain_size, pd);
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
        !xx_cru_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_cru_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
