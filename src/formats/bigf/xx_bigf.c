/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EA BIG archives.
 *
 *   0..3    "BIGF"
 *   4..7    total size, unused here
 *   8..11   u32 BE member count
 *   12..15  header size, unused here
 *
 * then one entry per member:
 *
 *   u32 BE data offset | u32 BE data size | NUL-terminated name
 *
 * Entries are variable length because the name is inline, so the directory is
 * walked in order rather than indexed.
 *
 * Some writers emit a final entry whose offset and size are both 0xCDCDCDCD --
 * uninitialised-memory filler that stands in for a member rather than being
 * one. It is recognised and stops the walk; treating it as a member would
 * invent a 3.4 GB entry out of padding.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bigf/xx_bigf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_BIGF_COPY_CHUNK (64 * 1024)

typedef struct xx_bigf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_bigf_member;

typedef struct xx_bigf_stream_s {
    xx_bigf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_bigf_stream;

static void xx_bigf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_bigf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_bigf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_bigf_path_safe(const char *name) {
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

static void xx_bigf_stream_free(void *pointer) {
    xx_bigf_stream *stream = (xx_bigf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_bigf_add(xx_bigf_stream *stream,
                          const xx_bigf_member *member) {
    xx_bigf_member *grown = (xx_bigf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_bigf_decode(Abstractformat *self,
                             const xx_bigf_member *member, uint8_t **out,
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
         !xx_bigf_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_BIGF_HEADER_SIZE 16
#define XX_BIGF_ENTRY_FIXED 8
#define XX_BIGF_SENTINEL 0xCDCDCDCDU
#define XX_BIGF_MAX_MEMBERS 100000
#define XX_BIGF_MAX_NAME 4096

static uint32_t xx_bigf_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint32_t xx_bigf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_bigf_stream *xx_bigf_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_bigf_stream *stream;
    uint8_t header[XX_BIGF_HEADER_SIZE];
    uint8_t entry[XX_BIGF_ENTRY_FIXED];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BIGF_HEADER_SIZE) return NULL;
    if (!xx_bigf_read_at(self, self->base_address, header, sizeof(header)) ||
        header[0] != 'B' || header[1] != 'I' || header[2] != 'G' ||
        header[3] != 'F') {
        return NULL;
    }
    count = (int64_t)(int32_t)xx_bigf_be32(header + 8);
    if (count <= 0 || count > XX_BIGF_MAX_MEMBERS) return NULL;

    stream = (xx_bigf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_BIGF_HEADER_SIZE;
    for (index = 0; index < count; ++index) {
        xx_bigf_member member;
        char *name;
        size_t name_length = 0U;
        int64_t data_offset;
        int64_t data_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_bigf_range_within(span, offset, XX_BIGF_ENTRY_FIXED) ||
            !xx_bigf_read_at(self, self->base_address + offset, entry,
                             sizeof(entry))) {
            goto fail;
        }
        offset += XX_BIGF_ENTRY_FIXED;

        /* Uninitialised-memory filler standing in for the last entry. */
        if (index == count - 1 &&
            xx_bigf_le32(entry) == XX_BIGF_SENTINEL &&
            xx_bigf_le32(entry + 4) == XX_BIGF_SENTINEL) {
            break;
        }

        data_offset = (int64_t)(int32_t)xx_bigf_be32(entry);
        data_size = (int64_t)(int32_t)xx_bigf_be32(entry + 4);
        if (data_offset < 0 || data_size < 0) goto fail;

        /* The name is inline and NUL terminated, so its length is only known
         * by reading it. */
        name = (char *)xx_mem_alloc(XX_BIGF_MAX_NAME + 1U);
        if (!name) goto fail;
        for (;;) {
            uint8_t byte;
            if (name_length > XX_BIGF_MAX_NAME || offset >= span ||
                !xx_bigf_read_at(self, self->base_address + offset, &byte,
                                 1U)) {
                xx_str_free(name);
                goto fail;
            }
            ++offset;
            if (byte == 0U) break;
            if (byte < 0x20U || byte > 0x7EU) {
                xx_str_free(name);
                goto fail;
            }
            name[name_length++] = (char)byte;
        }
        name[name_length] = '\0';
        if (name_length == 0U ||
            !xx_bigf_range_within(span, data_offset, data_size)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_bigf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_bigf_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_bigf_init(xx_bigf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_BIGF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ea-big");
    xx_format_set_extension(&archive->format, "big");
    archive->format.check_is_valid = xx_bigf_check_is_valid;
    archive->format.handle_base_info = xx_bigf_handle_base_info;
    archive->format.get_format_size = xx_bigf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bigf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bigf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bigf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bigf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bigf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bigf_free_archive_records_reading;
    archive->format.destroy = xx_bigf_vtable_destroy;
}

xx_bigf *xx_bigf_create(xx_io_device *device, int64_t base_address) {
    xx_bigf *archive = (xx_bigf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bigf_init(archive, device, base_address);
    return archive;
}

void xx_bigf_destroy(xx_bigf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_bigf_free(xx_bigf *archive) {
    if (!archive) return;
    xx_bigf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bigf_vtable_destroy(Abstractformat *self) {
    xx_bigf_destroy((xx_bigf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_bigf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bigf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_bigf_parse(self, pd);
    if (!stream) return false;
    xx_bigf_stream_free(stream);
    return true;
}

bool xx_bigf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bigf *archive = (xx_bigf *)self;
    xx_bigf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_bigf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_bigf_stream_free(stream);
    return true;
}

int64_t xx_bigf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bigf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_bigf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_bigf_set_record(xx_archive_record *record,
                                 const xx_bigf_member *member) {
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

static bool xx_bigf_copy_options(xx_list_s *target,
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

static const xx_var *xx_bigf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_bigf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_bigf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_bigf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bigf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_bigf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_bigf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_bigf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_bigf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bigf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bigf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bigf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_bigf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bigf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bigf_stream *stream;
    const xx_bigf_member *member;
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
    stream = (xx_bigf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bigf_path_safe(member->name)) return false;

    path_option = xx_bigf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_bigf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_bigf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_bigf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
