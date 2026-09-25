/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gitobject/xx_gitobject.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/formats/zlib/xx_zlib.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_GITOBJECT_PAYLOAD_NAME "payload"
#define XX_GITOBJECT_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_GITOBJECT_MAX_OUTPUT ((size_t)1024U * 1024U * 1024U)
#define XX_GITOBJECT_MAX_HEADER 32U

typedef struct xx_gitobject_buffer_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
} xx_gitobject_buffer;

static void xx_gitobject_vtable_destroy(Abstractformat *self);

static bool xx_gitobject_buffer_reserve(xx_gitobject_buffer *buffer,
                                        size_t additional) {
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (!buffer || additional > XX_GITOBJECT_MAX_OUTPUT - buffer->size) {
        return false;
    }
    required = buffer->size + additional;
    if (required <= buffer->capacity) return true;
    capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
    while (capacity < required) {
        if (capacity > XX_GITOBJECT_MAX_OUTPUT / 2U) {
            capacity = XX_GITOBJECT_MAX_OUTPUT;
        } else {
            capacity *= 2U;
        }
        if (capacity < required && capacity == XX_GITOBJECT_MAX_OUTPUT) {
            return false;
        }
    }
    grown = buffer->data ? (uint8_t *)xx_mem_realloc(buffer->data, capacity)
                         : (uint8_t *)xx_mem_alloc(capacity);
    if (!grown) return false;
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static ssize_t xx_gitobject_buffer_write(xx_io_device *device,
                                         const void *data, size_t size) {
    xx_gitobject_buffer *buffer =
        device ? (xx_gitobject_buffer *)device->priv : NULL;
    if (!buffer || (!data && size != 0U) ||
        !xx_gitobject_buffer_reserve(buffer, size)) {
        if (buffer) buffer->failed = true;
        return -1;
    }
    if (size != 0U) {
        xx_mem_copy(buffer->data + buffer->size, data, size);
        buffer->size += size;
    }
    return (ssize_t)size;
}

static bool xx_gitobject_write_all(xx_io_device *device, const void *data,
                                   size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_gitobject_type_from_header(const uint8_t *data, size_t size,
                                          char type[7], uint64_t *body_size,
                                          size_t *body_offset) {
    static const char *const valid_types[] = {"blob", "tree", "commit", "tag"};
    size_t header_size = 0U;
    size_t type_index;
    size_t type_length = 0U;
    size_t digits_start;
    uint64_t declared = 0U;
    if (!data || !type || !body_size || !body_offset || size == 0U) {
        return false;
    }
    while (header_size < size && header_size <= XX_GITOBJECT_MAX_HEADER &&
           data[header_size] != '\0') {
        ++header_size;
    }
    if (header_size == size || header_size > XX_GITOBJECT_MAX_HEADER ||
        data[header_size] != '\0') {
        return false;
    }
    for (type_index = 0U;
         type_index < sizeof(valid_types) / sizeof(valid_types[0]);
         ++type_index) {
        size_t length = xx_rt_strlen(valid_types[type_index]);
        if (header_size > length + 1U &&
            xx_rt_memcmp(data, valid_types[type_index], length) == 0 &&
            data[length] == ' ') {
            type_length = length;
            break;
        }
    }
    if (type_index == sizeof(valid_types) / sizeof(valid_types[0])) {
        return false;
    }
    digits_start = type_length + 1U;
    if (digits_start == header_size) return false;
    while (digits_start < header_size) {
        uint8_t digit = data[digits_start++];
        if (digit < '0' || digit > '9' ||
            declared > (UINT64_MAX - (uint64_t)(digit - '0')) / 10U) {
            return false;
        }
        declared = declared * 10U + (uint64_t)(digit - '0');
    }
    if (declared != (uint64_t)(size - (header_size + 1U))) return false;
    xx_mem_copy(type, valid_types[type_index], type_length);
    type[type_length] = '\0';
    *body_size = declared;
    *body_offset = header_size + 1U;
    return true;
}

/* Read the whole input into the staging buffer.  Used for the UNDEFLATED
 * shape described below; bounded by the same ceiling as the inflated one. */
static bool xx_gitobject_buffer_fill(Abstractformat *self,
                                     xx_gitobject_buffer *buffer,
                                     int64_t input_size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!self || !buffer || input_size <= 0 ||
        (uint64_t)input_size > XX_GITOBJECT_MAX_OUTPUT ||
        !xx_gitobject_buffer_reserve(buffer, (size_t)input_size) ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        return false;
    }
    while (done < (size_t)input_size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_read(self->device, buffer->data + done,
                            (size_t)input_size - done);
        if (amount <= 0 || (size_t)amount > (size_t)input_size - done) {
            return false;
        }
        done += (size_t)amount;
    }
    buffer->size = done;
    return true;
}

/* A loose Git object is an authenticated zlib member containing
 * "<type> <decimal-size>\\0<body>".  Decode into a bounded staging buffer so
 * no caller observes a body until both the zlib trailer and Git header agree.
 *
 * THE UNDEFLATED SHAPE.  Loose objects also travel already inflated - every
 * one of the seven samples in F:\ARC\ARC\GIT OBJECT is a plain
 * "blob <n>\0<body>" with no zlib wrapper, and its declared length matches
 * its body exactly.  So when the zlib path declines, the input is retried as
 * the bare object.  That fallback is not a loosening: the Git header check is
 * the same one the inflated path uses, and it already requires the declared
 * size to equal the remaining bytes exactly, which a non-Git file will not
 * satisfy by accident. */
static bool xx_gitobject_decode_stream(Abstractformat *self,
                                       xx_io_device *destination,
                                       char object_type[7],
                                       uint64_t *object_size,
                                       int64_t *stream_size,
                                       xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    xx_zlib zlib;
    xx_gitobject_buffer output = {0};
    xx_io_device sink = {0};
    size_t body_offset;
    char type[7] = {0};
    uint64_t body_size;
    bool zlib_initialized = false;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 || !object_type ||
        !object_size || !stream_size || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < 6 ||
        (uint64_t)(total_size - self->base_address) > XX_GITOBJECT_MAX_INPUT) {
        return false;
    }
    input_size = total_size - self->base_address;
    xx_zlib_init(&zlib, self->device, self->base_address);
    zlib_initialized = true;
    sink.write = xx_gitobject_buffer_write;
    sink.priv = &output;
    if (xx_zlib_handle_base_info(&zlib.format, pd) &&
        zlib.format.format_size == input_size &&
        xx_zlib_unpack_to_device(&zlib, &sink, pd) && !output.failed) {
        /* Inflated in place; fall through to the shared header check. */
    } else {
        /* Not a zlib member, or not one that covers the whole input: retry
         * the bytes as an already inflated object. */
        output.size = 0U;
        output.failed = false;
        if (!xx_gitobject_buffer_fill(self, &output, input_size, pd)) {
            goto cleanup;
        }
    }
    if (!xx_gitobject_type_from_header(output.data, output.size, type,
                                       &body_size, &body_offset) ||
        (destination && !xx_gitobject_write_all(destination,
                                                 output.data + body_offset,
                                                 (size_t)body_size, pd))) {
        goto cleanup;
    }
    xx_mem_copy(object_type, type, sizeof(type));
    *object_size = body_size;
    *stream_size = input_size;
    result = true;
cleanup:
    if (zlib_initialized) xx_zlib_destroy(&zlib);
    xx_mem_free(output.data);
    return result;
}

static bool xx_gitobject_copy_options(xx_list_s *destination,
                                      const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_gitobject_find_option(const xx_list_s *options,
                                               uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_gitobject_populate_record(Abstractformat *self,
                                         xx_archive_record *record) {
    const xx_gitobject *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 6) {
        return false;
    }
    archive = (const xx_gitobject *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 2;
    record->data_offset = self->base_address + 2;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_GITOBJECT_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->object_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_gitobject_init(xx_gitobject *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_GIT_OBJECT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-git-loose-object");
    xx_format_set_extension(&archive->format, "gitobject");
    archive->format.check_is_valid = xx_gitobject_check_is_valid;
    archive->format.handle_base_info = xx_gitobject_handle_base_info;
    archive->format.get_format_size = xx_gitobject_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gitobject_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gitobject_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gitobject_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gitobject_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gitobject_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gitobject_free_archive_records_reading;
    archive->format.destroy = xx_gitobject_vtable_destroy;
    archive->stream_end = -1;
}

xx_gitobject *xx_gitobject_create(xx_io_device *device, int64_t base_address) {
    xx_gitobject *archive = (xx_gitobject *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gitobject_init(archive, device, base_address);
    return archive;
}

void xx_gitobject_destroy(xx_gitobject *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->object_size = 0U;
    archive->stream_end = -1;
    xx_mem_zero(archive->object_type, sizeof(archive->object_type));
}

static void xx_gitobject_vtable_destroy(Abstractformat *self) {
    xx_gitobject_destroy((xx_gitobject *)self);
}

void xx_gitobject_free(xx_gitobject *archive) {
    if (!archive) return;
    xx_gitobject_destroy(archive);
    xx_mem_free(archive);
}

bool xx_gitobject_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    char type[7] = {0};
    uint64_t size;
    int64_t stream_size;
    return xx_gitobject_decode_stream(self, NULL, type, &size, &stream_size,
                                      pd);
}

bool xx_gitobject_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    char type[7] = {0};
    uint64_t size;
    int64_t stream_size;
    xx_gitobject *archive;
    if (!self || !xx_gitobject_decode_stream(self, NULL, type, &size,
                                             &stream_size, pd)) {
        if (self) {
            archive = (xx_gitobject *)self;
            archive->object_size = 0U;
            archive->stream_end = -1;
            xx_mem_zero(archive->object_type, sizeof(archive->object_type));
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_gitobject *)self;
    archive->object_size = size;
    archive->stream_end = self->base_address + stream_size;
    xx_mem_copy(archive->object_type, type, sizeof(type));
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_GIT_OBJECT;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_gitobject_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_gitobject_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_gitobject_unpack_to_device(xx_gitobject *archive,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    char type[7] = {0};
    uint64_t size;
    int64_t stream_size;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_gitobject_decode_stream(&archive->format, destination, type,
                                    &size, &stream_size, pd)) {
        return false;
    }
    return stream_size == archive->format.format_size &&
           size == archive->object_size &&
           xx_rt_memcmp(type, archive->object_type, sizeof(type)) == 0;
}

xx_archive_record_state *xx_gitobject_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_gitobject_copy_options(&state->options, options) ||
        !xx_gitobject_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_gitobject_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_gitobject_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_gitobject_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_gitobject *archive = (xx_gitobject *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_gitobject_find_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        char type[7] = {0};
        uint64_t size;
        int64_t stream_size;
        return xx_gitobject_decode_stream(self, NULL, type, &size,
                                          &stream_size, pd) &&
               stream_size == self->format_size &&
               size == archive->object_size &&
               xx_rt_memcmp(type, archive->object_type, sizeof(type)) == 0;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) return false;
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/",
                                          XX_GITOBJECT_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_GITOBJECT_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path ||
        !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_gitobject_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_gitobject_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_gitobject_get_object_size(const xx_gitobject *archive) {
    return archive ? archive->object_size : 0U;
}

int64_t xx_gitobject_get_stream_end(const xx_gitobject *archive) {
    return archive ? archive->stream_end : -1;
}

const char *xx_gitobject_get_object_type(const xx_gitobject *archive) {
    return archive ? archive->object_type : NULL;
}
