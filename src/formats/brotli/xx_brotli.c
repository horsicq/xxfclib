/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/brotli/xx_brotli.h"

#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_BROTLI_PAYLOAD_NAME "payload"
#define XX_BROTLI_MAX_STREAM_SIZE ((uint64_t)1024U * 1024U * 1024U)

static void xx_brotli_vtable_destroy(Abstractformat *self);

static bool xx_brotli_read_exact_at(xx_io_device *device, int64_t offset,
                                    void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_brotli_write_all(xx_io_device *device, const void *data,
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

static bool xx_brotli_decode_stream(Abstractformat *self,
                                    xx_io_device *destination,
                                    uint64_t *uncompressed_size,
                                    int64_t *stream_size, xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t output_size = 0U;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !stream_size || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size == self->base_address ||
        (uint64_t)(total_size - self->base_address) >
            XX_BROTLI_MAX_STREAM_SIZE ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_brotli_read_exact_at(self->device, self->base_address,
                                           input, (size_t)input_size) ||
        !xx_brotli_decompress_alloc(input, (size_t)input_size, &output,
                                    &output_size) ||
        (destination && !xx_brotli_write_all(destination, output, output_size,
                                              pd))) {
        goto cleanup;
    }
    *uncompressed_size = (uint64_t)output_size;
    *stream_size = input_size;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

static bool xx_brotli_copy_options(xx_list_s *destination,
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

static const xx_var *xx_brotli_find_option(const xx_list_s *options,
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

static bool xx_brotli_populate_record(Abstractformat *self,
                                      xx_archive_record *record) {
    const xx_brotli *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= 0) return false;
    archive = (const xx_brotli *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 0;
    record->data_offset = self->base_address;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_BROTLI_PAYLOAD_NAME) &&
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

void xx_brotli_init(xx_brotli *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_BROTLI;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-brotli");
    xx_format_set_extension(&archive->format, "br");
    archive->format.check_is_valid = xx_brotli_check_is_valid;
    archive->format.handle_base_info = xx_brotli_handle_base_info;
    archive->format.get_format_size = xx_brotli_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_brotli_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_brotli_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_brotli_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_brotli_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_brotli_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_brotli_free_archive_records_reading;
    archive->format.destroy = xx_brotli_vtable_destroy;
    archive->stream_end = -1;
}

xx_brotli *xx_brotli_create(xx_io_device *device, int64_t base_address) {
    xx_brotli *archive = (xx_brotli *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_brotli_init(archive, device, base_address);
    return archive;
}

void xx_brotli_destroy(xx_brotli *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_brotli_vtable_destroy(Abstractformat *self) {
    xx_brotli_destroy((xx_brotli *)self);
}

void xx_brotli_free(xx_brotli *archive) {
    if (!archive) return;
    xx_brotli_destroy(archive);
    xx_mem_free(archive);
}

bool xx_brotli_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    return xx_brotli_decode_stream(self, NULL, &size, &stream_size, pd);
}

bool xx_brotli_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    int64_t total_size;
    xx_brotli *archive;
    if (!self || !xx_brotli_decode_stream(self, NULL, &size, &stream_size,
                                          pd)) {
        if (self) {
            archive = (xx_brotli *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
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
    archive = (xx_brotli *)self;
    archive->uncompressed_size = size;
    archive->stream_end = self->base_address + stream_size;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = archive->stream_end < total_size ?
                               archive->stream_end : -1;
    self->overlay_size = archive->stream_end < total_size ?
                             total_size - archive->stream_end : 0;
    self->file_type = XX_FILE_TYPE_BROTLI;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_brotli_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_brotli_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_brotli_unpack_to_device(xx_brotli *archive,
                                xx_io_device *destination,
                                xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_brotli_decode_stream(&archive->format, destination, &size,
                                 &stream_size, pd)) return false;
    return stream_size == archive->format.format_size &&
           size == archive->uncompressed_size;
}

xx_archive_record_state *xx_brotli_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_brotli_copy_options(&state->options, options) ||
        !xx_brotli_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_brotli_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_brotli_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_brotli_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_brotli *archive = (xx_brotli *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_brotli_find_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        uint64_t size;
        int64_t compressed_size;
        return xx_brotli_decode_stream(self, NULL, &size, &compressed_size,
                                       pd) && compressed_size == self->format_size;
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
        destination_path = xx_str_concat3(base_path, "/", XX_BROTLI_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_BROTLI_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_brotli_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_brotli_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_brotli_get_uncompressed_size(const xx_brotli *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_brotli_get_stream_end(const xx_brotli *archive) {
    return archive ? archive->stream_end : -1;
}
