/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Descent 3 HOG2 archives (".hog", ".mn3", and the ".d3c"/".d3m" module and
 * mission files that use the same container).  Ported from XArchive's
 * games/xhog2.cpp; the same layout is described by the Descent 3 source
 * release.  It is a different format from the Descent 1/2 "DHF" HOG.
 *
 *   header, 68 bytes at offset 0:
 *     0x00   4  char[4]  magic "HOG2"
 *     0x04   4  u32 LE   member count
 *     0x08   4  u32 LE   offset of the first payload, which always equals
 *                        68 + count * 48
 *     0x0c  56  bytes    reserved, 0xff filled in every sample
 *
 *   table, 48 bytes per member, starting at 68:
 *     +0x00 36  char[36] file name, NUL padded
 *     +0x24  4  u32 LE   flags; they do not select a compression method
 *     +0x28  4  u32 LE   payload size
 *     +0x2c  4  u32 LE   Unix modification time
 *
 *   payloads follow the table back to back in table order and are stored
 *   verbatim - the container compresses nothing.
 *
 * The magic identifies the file, and the declared first-payload offset is a
 * free consistency check on the count: it must agree with 68 + count * 48
 * exactly.  Every payload is bounded against the real file size before use,
 * and the running offset must land exactly on end-of-file after the last
 * member.  All 9 corpus samples in F:\ARC\ARC\HOG2 do.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hog2/xx_hog2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef HOG2
#define XX_HOG2_FILE_TYPE XX_FILE_TYPE_HOG2
#else
#define XX_HOG2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_HOG2_METHOD_STORE 0U
#define XX_HOG2_HEADER_SIZE 68
#define XX_HOG2_ENTRY_SIZE 48
#define XX_HOG2_NAME_SIZE 36
/* 100k members is the reference implementation's cap; the table-size bound
 * against the real file size is what actually limits the allocation. */
#define XX_HOG2_MAX_MEMBERS 100000U

typedef struct xx_hog2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_hog2_member;

typedef struct xx_hog2_stream_s {
    xx_hog2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_hog2_stream;

static void xx_hog2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_hog2_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_hog2_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_hog2_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
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

/* The 36 name bytes are NUL-padded 8-bit text.  Descent 3 uses flat names,
 * but a byte a filesystem would object to still becomes '_'. */
static char *xx_hog2_make_name(const uint8_t *raw) {
    char text[XX_HOG2_NAME_SIZE + 1];
    size_t length = 0U;
    size_t index;

    while (length < XX_HOG2_NAME_SIZE && raw[length] != 0x00U) ++length;
    while (length != 0U && raw[length - 1U] == 0x20U) --length;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[index] = '_';
        } else {
            text[index] = (char)c;
        }
    }
    while (length != 0U && text[length - 1U] == '.') --length;
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return xx_str_dup(text);
}

static void xx_hog2_stream_free(void *pointer) {
    xx_hog2_stream *stream = (xx_hog2_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_hog2_stream *xx_hog2_parse(Abstractformat *self,
                                     xx_pd_struct *pd) {
    xx_hog2_stream *stream = NULL;
    uint8_t head[XX_HOG2_HEADER_SIZE];
    uint8_t *table = NULL;
    int64_t total;
    int64_t span;
    int64_t table_size;
    int64_t cursor;
    uint64_t count;
    uint64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_HOG2_HEADER_SIZE) return NULL;
    if (!xx_hog2_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    if (xx_rt_memcmp(head, "HOG2", 4U) != 0) return NULL;

    /* The count is bounded before it is multiplied out, and the product is
     * then bounded against the real file size before anything is allocated. */
    count = (uint64_t)xx_hog2_le32(head + 4);
    if (count == 0U || count > XX_HOG2_MAX_MEMBERS) return NULL;
    table_size = (int64_t)(count * XX_HOG2_ENTRY_SIZE);
    if (table_size > span - XX_HOG2_HEADER_SIZE) return NULL;
    /* The header republishes where the payloads start; it must agree with the
     * count exactly, which is the cheapest check there is on the count. */
    if ((int64_t)xx_hog2_le32(head + 8) != XX_HOG2_HEADER_SIZE + table_size) {
        return NULL;
    }

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return NULL;
    if (!xx_hog2_read_at(self, self->base_address + XX_HOG2_HEADER_SIZE, table,
                         (size_t)table_size)) {
        goto fail;
    }

    stream = (xx_hog2_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_hog2_member *)xx_mem_alloc(sizeof(*stream->items) *
                                                   (size_t)count);
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items) * (size_t)count);

    cursor = XX_HOG2_HEADER_SIZE + table_size;
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = table + (size_t)(index * XX_HOG2_ENTRY_SIZE);
        int64_t size = (int64_t)xx_hog2_le32(entry + 40);

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (size < 0 || size > span - cursor) goto fail;

        stream->items[index].name = xx_hog2_make_name(entry);
        if (!stream->items[index].name) goto fail;
        stream->items[index].header_offset =
            self->base_address + XX_HOG2_HEADER_SIZE +
            (int64_t)(index * XX_HOG2_ENTRY_SIZE);
        stream->items[index].header_size = XX_HOG2_ENTRY_SIZE;
        stream->items[index].data_offset = self->base_address + cursor;
        stream->items[index].size = size;
        ++stream->count;
        cursor += size;
    }

    /* The payloads are contiguous and fill the file; a leftover tail means
     * the table did not describe this file. */
    if (cursor != span) goto fail;

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_hog2_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read.  The length
 * comes from offsets that parse already proved lie inside the file. */
static bool xx_hog2_decode(Abstractformat *self,
                            const xx_hog2_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A zero-length member is legitimate - the SINNER corpus carries one -
     * and must extract as an empty file rather than failing the unpack. */
    if (member->size == 0) return true;
    if ((uint64_t)member->size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (!xx_hog2_read_at(self, member->data_offset, output,
                          (size_t)member->size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_hog2_init(xx_hog2 *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HOG2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-descent-hog2");
    xx_format_set_extension(&archive->format, "hog");
    archive->format.check_is_valid = xx_hog2_check_is_valid;
    archive->format.handle_base_info = xx_hog2_handle_base_info;
    archive->format.get_format_size = xx_hog2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hog2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hog2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hog2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hog2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hog2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hog2_free_archive_records_reading;
    archive->format.destroy = xx_hog2_vtable_destroy;
}

xx_hog2 *xx_hog2_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_hog2 *archive = (xx_hog2 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_hog2_init(archive, device, base_address);
    return archive;
}

void xx_hog2_destroy(xx_hog2 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_hog2_free(xx_hog2 *archive) {
    if (!archive) return;
    xx_hog2_destroy(archive);
    xx_mem_free(archive);
}

static void xx_hog2_vtable_destroy(Abstractformat *self) {
    xx_hog2_destroy((xx_hog2 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_hog2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_hog2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_hog2_parse(self, pd);
    if (!stream) return false;
    xx_hog2_stream_free(stream);
    return true;
}

bool xx_hog2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hog2 *archive = (xx_hog2 *)self;
    xx_hog2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_hog2_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_hog2_stream_free(stream);
    return true;
}

int64_t xx_hog2_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_hog2_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_hog2 *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_hog2_set_record(xx_archive_record *record,
                                     const xx_hog2_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_HOG2_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_hog2_copy_options(xx_list_s *target,
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

static const xx_var *xx_hog2_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_hog2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_hog2_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_hog2_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_hog2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_hog2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_hog2_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_hog2_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_hog2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_hog2_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_hog2_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_hog2_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_hog2_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hog2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_hog2_stream *stream;
    const xx_hog2_member *member;
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
    stream = (xx_hog2_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_hog2_path_safe(member->name)) return false;

    path_option = xx_hog2_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_hog2_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_hog2_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_hog2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
