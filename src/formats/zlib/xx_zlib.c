/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zlib/xx_zlib.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_ZLIB_PAYLOAD_NAME "payload"
#define XX_ZLIB_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_ZLIB_MAX_OUTPUT ((size_t)1024U * 1024U * 1024U)
#define XX_ZLIB_ADLER_MOD UINT32_C(65521)

typedef struct xx_zlib_buffer_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool failed;
} xx_zlib_buffer;

static void xx_zlib_vtable_destroy(Abstractformat *self);

static bool xx_zlib_read_exact_at(xx_io_device *device, int64_t offset,
                                  void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_zlib_write_all(xx_io_device *device, const void *data,
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

static bool xx_zlib_header_is_valid(const uint8_t *input, size_t input_size) {
    uint16_t header;
    if (!input || input_size < 6U) return false;
    header = (uint16_t)((uint16_t)input[0] << 8U) | input[1];
    return (input[0] & UINT8_C(0x0f)) == UINT8_C(8) &&
           (input[0] >> 4U) <= 7U && (header % 31U) == 0U &&
           (input[1] & UINT8_C(0x20)) == 0U;
}

static uint32_t xx_zlib_read32be(const uint8_t *input) {
    return ((uint32_t)input[0] << 24U) | ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) | (uint32_t)input[3];
}

static uint32_t xx_zlib_adler32(const uint8_t *data, size_t size) {
    uint32_t a = 1U;
    uint32_t b = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        a += data[index];
        if (a >= XX_ZLIB_ADLER_MOD) a -= XX_ZLIB_ADLER_MOD;
        b += a;
        if (b >= XX_ZLIB_ADLER_MOD) b -= XX_ZLIB_ADLER_MOD;
    }
    return (b << 16U) | a;
}

static bool xx_zlib_buffer_reserve(xx_zlib_buffer *buffer, size_t additional) {
    size_t required;
    size_t capacity;
    uint8_t *grown;
    if (!buffer || additional > XX_ZLIB_MAX_OUTPUT - buffer->size) {
        return false;
    }
    required = buffer->size + additional;
    if (required <= buffer->capacity) return true;
    capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
    while (capacity < required) {
        if (capacity > XX_ZLIB_MAX_OUTPUT / 2U) {
            capacity = XX_ZLIB_MAX_OUTPUT;
        } else {
            capacity *= 2U;
        }
        if (capacity < required && capacity == XX_ZLIB_MAX_OUTPUT) return false;
    }
    grown = buffer->data ? (uint8_t *)xx_mem_realloc(buffer->data, capacity) :
                           (uint8_t *)xx_mem_alloc(capacity);
    if (!grown) return false;
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static ssize_t xx_zlib_buffer_write(xx_io_device *device, const void *data,
                                    size_t size) {
    xx_zlib_buffer *buffer = device ? (xx_zlib_buffer *)device->priv : NULL;
    size_t index;
    if (!buffer || (!data && size != 0U) ||
        !xx_zlib_buffer_reserve(buffer, size)) {
        if (buffer) buffer->failed = true;
        return -1;
    }
    for (index = 0U; index < size; ++index) {
        buffer->data[buffer->size + index] = ((const uint8_t *)data)[index];
    }
    buffer->size += size;
    return (ssize_t)size;
}

static bool xx_zlib_decode_stream(Abstractformat *self,
                                  xx_io_device *destination,
                                  uint64_t *uncompressed_size,
                                  int64_t *stream_size, uint32_t *adler32,
                                  xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    xx_zlib_buffer output = {0};
    xx_io_device sink = {0};
    size_t raw_size = 0U;
    size_t trailer_offset;
    uint32_t expected_adler;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !stream_size || !adler32 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < 6 ||
        (uint64_t)(total_size - self->base_address) > XX_ZLIB_MAX_INPUT ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_zlib_read_exact_at(self->device, self->base_address,
                                         input, (size_t)input_size) ||
        !xx_zlib_header_is_valid(input, (size_t)input_size)) {
        goto cleanup;
    }
    sink.write = xx_zlib_buffer_write;
    sink.priv = &output;
    if (!xx_deflate_unpack_memory_to_device_ex(input + 2U,
                                                (size_t)input_size - 2U,
                                                &sink, &raw_size, false, pd) ||
        output.failed || raw_size > (size_t)input_size - 6U ||
        raw_size > SIZE_MAX - 2U) {
        goto cleanup;
    }
    trailer_offset = 2U + raw_size;
    if (trailer_offset > (size_t)input_size - 4U) goto cleanup;
    expected_adler = xx_zlib_read32be(input + trailer_offset);
    if (xx_zlib_adler32(output.data, output.size) != expected_adler ||
        (destination && !xx_zlib_write_all(destination, output.data,
                                            output.size, pd))) {
        goto cleanup;
    }
    *uncompressed_size = (uint64_t)output.size;
    *stream_size = (int64_t)(trailer_offset + 4U);
    *adler32 = expected_adler;
    result = true;
cleanup:
    xx_mem_free(output.data);
    xx_mem_free(input);
    return result;
}

static bool xx_zlib_copy_options(xx_list_s *destination,
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

static const xx_var *xx_zlib_find_option(const xx_list_s *options,
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

static bool xx_zlib_populate_record(Abstractformat *self,
                                    xx_archive_record *record) {
    const xx_zlib *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 6) {
        return false;
    }
    archive = (const xx_zlib *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 2;
    record->data_offset = self->base_address + 2;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_ZLIB_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_zlib_init(xx_zlib *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_ZLIB;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zlib");
    xx_format_set_extension(&archive->format, "zlib");
    archive->format.check_is_valid = xx_zlib_check_is_valid;
    archive->format.handle_base_info = xx_zlib_handle_base_info;
    archive->format.get_format_size = xx_zlib_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zlib_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zlib_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_zlib_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zlib_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zlib_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zlib_free_archive_records_reading;
    archive->format.destroy = xx_zlib_vtable_destroy;
    archive->stream_end = -1;
}

xx_zlib *xx_zlib_create(xx_io_device *device, int64_t base_address) {
    xx_zlib *archive = (xx_zlib *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_zlib_init(archive, device, base_address);
    return archive;
}

void xx_zlib_destroy(xx_zlib *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->adler32 = 0U;
}

static void xx_zlib_vtable_destroy(Abstractformat *self) {
    xx_zlib_destroy((xx_zlib *)self);
}

void xx_zlib_free(xx_zlib *archive) {
    if (!archive) return;
    xx_zlib_destroy(archive);
    xx_mem_free(archive);
}

bool xx_zlib_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    uint32_t adler32;
    return xx_zlib_decode_stream(self, NULL, &size, &stream_size, &adler32,
                                 pd);
}

bool xx_zlib_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    uint32_t adler32;
    int64_t total_size;
    xx_zlib *archive;
    if (!self || !xx_zlib_decode_stream(self, NULL, &size, &stream_size,
                                        &adler32, pd)) {
        if (self) {
            archive = (xx_zlib *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            archive->adler32 = 0U;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    total_size = xx_io_total_size(self->device);
    archive = (xx_zlib *)self;
    archive->uncompressed_size = size;
    archive->stream_end = self->base_address + stream_size;
    archive->adler32 = adler32;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = archive->stream_end < total_size ?
                               archive->stream_end : -1;
    self->overlay_size = archive->stream_end < total_size ?
                             total_size - archive->stream_end : 0;
    self->file_type = XX_FILE_TYPE_ZLIB;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_zlib_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_zlib_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_zlib_unpack_to_device(xx_zlib *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    uint32_t adler32;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_zlib_decode_stream(&archive->format, destination, &size,
                               &stream_size, &adler32, pd)) return false;
    return stream_size == archive->format.format_size &&
           size == archive->uncompressed_size && adler32 == archive->adler32;
}

xx_archive_record_state *xx_zlib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_zlib_copy_options(&state->options, options) ||
        !xx_zlib_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_zlib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_zlib_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_zlib_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_zlib *archive = (xx_zlib *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_zlib_find_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        uint64_t size;
        int64_t compressed_size;
        uint32_t adler32;
        return xx_zlib_decode_stream(self, NULL, &size, &compressed_size,
                                     &adler32, pd) &&
               compressed_size == self->format_size &&
               size == archive->uncompressed_size && adler32 == archive->adler32;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        destination_path = xx_str_concat3(base_path, "/", XX_ZLIB_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_ZLIB_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_zlib_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_zlib_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_zlib_get_uncompressed_size(const xx_zlib *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_zlib_get_stream_end(const xx_zlib *archive) {
    return archive ? archive->stream_end : -1;
}

uint32_t xx_zlib_get_adler32(const xx_zlib *archive) {
    return archive ? archive->adler32 : 0U;
}
