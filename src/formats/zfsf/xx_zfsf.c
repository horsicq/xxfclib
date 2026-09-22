/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZFSF archives.
 *
 *   header, 0x1c bytes:
 *     0x00  "ZFSF"
 *     0x04  u32 LE version, must be 1
 *     0x08  u32 LE, must be 0x10
 *     0x0c  i32 LE entries per group
 *     0x10  i32 LE total entries
 *     0x18  u32 LE offset of the first group, must be 0x1c
 *
 *   each group: u32 LE link to the next group, then up to "entries per group"
 *   records of 0x24 bytes:
 *     0x00  name, 0x10 bytes
 *     0x10  u32 LE data offset
 *     0x18  u32 LE data size
 *
 * The last group is partial. Reading a full group's worth of entries out of it
 * would manufacture members from whatever follows, so the walk tracks how many
 * entries remain and reads only that many.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zfsf/xx_zfsf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_ZFSF_COPY_CHUNK (64 * 1024)

typedef struct xx_zfsf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zfsf_member;

typedef struct xx_zfsf_stream_s {
    xx_zfsf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zfsf_stream;

static void xx_zfsf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zfsf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zfsf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zfsf_path_safe(const char *name) {
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

static void xx_zfsf_stream_free(void *pointer) {
    xx_zfsf_stream *stream = (xx_zfsf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zfsf_add(xx_zfsf_stream *stream,
                          const xx_zfsf_member *member) {
    xx_zfsf_member *grown = (xx_zfsf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_zfsf_decode(Abstractformat *self,
                             const xx_zfsf_member *member, uint8_t **out,
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
         !xx_zfsf_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_ZFSF_HEADER_SIZE 0x1c
#define XX_ZFSF_ENTRY_SIZE 0x24
#define XX_ZFSF_NAME_SIZE 0x10
#define XX_ZFSF_FIRST_GROUP 0x1c
#define XX_ZFSF_MAX_GROUPS 100000

static uint32_t xx_zfsf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_zfsf_stream *xx_zfsf_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zfsf_stream *stream;
    uint8_t header[XX_ZFSF_HEADER_SIZE];
    uint8_t entry[XX_ZFSF_ENTRY_SIZE];
    char name[XX_ZFSF_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t group_offset;
    int32_t group_capacity;
    int32_t remaining;
    int32_t groups_seen = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZFSF_HEADER_SIZE) return NULL;
    if (!xx_zfsf_read_at(self, self->base_address, header, sizeof(header)) ||
        header[0] != 'Z' || header[1] != 'F' || header[2] != 'S' ||
        header[3] != 'F' || xx_zfsf_le32(header + 4) != 1U ||
        xx_zfsf_le32(header + 8) != 0x10U ||
        xx_zfsf_le32(header + 24) != (uint32_t)XX_ZFSF_FIRST_GROUP) {
        return NULL;
    }
    group_capacity = (int32_t)xx_zfsf_le32(header + 12);
    remaining = (int32_t)xx_zfsf_le32(header + 16);
    if (group_capacity <= 0 || remaining < 0) return NULL;
    /* Every entry is a distinct record in the file, so a count that cannot
     * physically fit is a rejection rather than something to clamp. */
    if ((int64_t)remaining > span / XX_ZFSF_ENTRY_SIZE) return NULL;

    stream = (xx_zfsf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    group_offset = XX_ZFSF_FIRST_GROUP;
    while (remaining > 0) {
        uint8_t link[4];
        int64_t entry_offset;
        int32_t in_group;
        int32_t i;

        if ((pd && xx_pd_is_stopped(pd)) ||
            ++groups_seen > XX_ZFSF_MAX_GROUPS ||
            !xx_zfsf_range_within(span, group_offset, 4) ||
            !xx_zfsf_read_at(self, self->base_address + group_offset, link,
                             sizeof(link))) {
            goto fail;
        }
        entry_offset = group_offset + 4;
        /* The last group is partial: read only what is left. */
        in_group = group_capacity < remaining ? group_capacity : remaining;

        for (i = 0; i < in_group; ++i) {
            xx_zfsf_member member;
            int64_t data_offset;
            int64_t data_size;
            size_t length = 0U;

            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_zfsf_range_within(span, entry_offset,
                                      XX_ZFSF_ENTRY_SIZE) ||
                !xx_zfsf_read_at(self, self->base_address + entry_offset,
                                 entry, sizeof(entry))) {
                goto fail;
            }
            data_offset = (int64_t)(int32_t)xx_zfsf_le32(entry + 0x10);
            data_size = (int64_t)(int32_t)xx_zfsf_le32(entry + 0x18);
            if (data_offset < 0 || data_size < 0 ||
                !xx_zfsf_range_within(span, data_offset, data_size)) {
                goto fail;
            }
            /* The name field is fixed width and need not be terminated. */
            while (length < XX_ZFSF_NAME_SIZE && entry[length] != 0U) {
                if (entry[length] < 0x20U || entry[length] > 0x7EU) goto fail;
                name[length] = (char)entry[length];
                ++length;
            }
            name[length] = '\0';
            if (length == 0U) goto fail;

            xx_mem_zero(&member, sizeof(member));
            member.name = xx_str_dup(name);
            if (!member.name) goto fail;
            member.header_offset = self->base_address + entry_offset;
            member.header_size = XX_ZFSF_ENTRY_SIZE;
            member.data_offset = self->base_address + data_offset;
            member.compressed_size = data_size;
            member.uncompressed_size = data_size;
            if (!xx_zfsf_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            entry_offset += XX_ZFSF_ENTRY_SIZE;
            --remaining;
        }
        if (remaining > 0) {
            group_offset = (int64_t)xx_zfsf_le32(link);
            if (group_offset <= 0) goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_zfsf_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_zfsf_init(xx_zfsf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZFSF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zfsf");
    xx_format_set_extension(&archive->format, "zfsf");
    archive->format.check_is_valid = xx_zfsf_check_is_valid;
    archive->format.handle_base_info = xx_zfsf_handle_base_info;
    archive->format.get_format_size = xx_zfsf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zfsf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zfsf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zfsf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zfsf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zfsf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zfsf_free_archive_records_reading;
    archive->format.destroy = xx_zfsf_vtable_destroy;
}

xx_zfsf *xx_zfsf_create(xx_io_device *device, int64_t base_address) {
    xx_zfsf *archive = (xx_zfsf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zfsf_init(archive, device, base_address);
    return archive;
}

void xx_zfsf_destroy(xx_zfsf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zfsf_free(xx_zfsf *archive) {
    if (!archive) return;
    xx_zfsf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zfsf_vtable_destroy(Abstractformat *self) {
    xx_zfsf_destroy((xx_zfsf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zfsf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zfsf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zfsf_parse(self, pd);
    if (!stream) return false;
    xx_zfsf_stream_free(stream);
    return true;
}

bool xx_zfsf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zfsf *archive = (xx_zfsf *)self;
    xx_zfsf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zfsf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zfsf_stream_free(stream);
    return true;
}

int64_t xx_zfsf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zfsf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zfsf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zfsf_set_record(xx_archive_record *record,
                                 const xx_zfsf_member *member) {
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

static bool xx_zfsf_copy_options(xx_list_s *target,
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

static const xx_var *xx_zfsf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zfsf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zfsf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zfsf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zfsf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zfsf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zfsf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zfsf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zfsf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zfsf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zfsf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zfsf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zfsf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zfsf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zfsf_stream *stream;
    const xx_zfsf_member *member;
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
    stream = (xx_zfsf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zfsf_path_safe(member->name)) return false;

    path_option = xx_zfsf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zfsf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zfsf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zfsf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
