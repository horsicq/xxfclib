/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LZPIS2 preserves no member name, so the archive view exposes one entry
 * called "payload".  The complete physical stream is the member; trailing
 * bytes are not permitted by the container grammar.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzpis2/xx_lzpis2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_LZPIS2_PAYLOAD_NAME "payload"
#define XX_LZPIS2_MAX_INPUT ((uint64_t)0x10000000U)

typedef struct xx_lzpis2_context_s {
    uint64_t uncompressed_size;
    int64_t stream_size;
    uint32_t chunk_count;
} xx_lzpis2_context;

static void xx_lzpis2_vtable_destroy(Abstractformat *self);

static bool xx_lzpis2_read_at(xx_io_device *device, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_lzpis2_write_all(xx_io_device *device, const uint8_t *buffer,
                                size_t size, xx_pd_struct *pd) {
    size_t completed = 0U;
    if (!device || (!buffer && size != 0U)) return false;
    while (completed < size) {
        ssize_t sent;
        if (pd && xx_pd_is_stopped(pd)) return false;
        sent = xx_io_write(device, buffer + completed, size - completed);
        if (sent <= 0 || (size_t)sent > size - completed) return false;
        completed += (size_t)sent;
    }
    return true;
}

/*
 * Decode to an optional destination.  Validation uses the same bounded
 * decoder as extraction so a magic-only false positive is never classified as
 * LZPIS2 by the generic format detector.
 */
static bool xx_lzpis2_process(Abstractformat *self, xx_io_device *destination,
                              xx_lzpis2_context *context,
                              xx_pd_struct *pd) {
    int64_t total_size;
    int64_t span;
    uint8_t *input = NULL;
    uint8_t *decoded = NULL;
    xx_lzpis2_info info;
    size_t consumed = 0U;
    bool result = false;

    if (!self || !self->device || !context || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    span = total_size - self->base_address;
    if (span <= 0 || (uint64_t)span > XX_LZPIS2_MAX_INPUT ||
        (uint64_t)span > (uint64_t)SIZE_MAX) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!input || !xx_lzpis2_read_at(self->device, self->base_address, input,
                                     (size_t)span) ||
        !xx_lzpis2_parse_memory(input, (size_t)span, &info) ||
        info.uncompressed_size > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }
    decoded = (uint8_t *)xx_mem_alloc((size_t)info.uncompressed_size);
    if (!decoded ||
        !xx_lzpis2_decompress_memory(input, (size_t)span, decoded,
                                     (size_t)info.uncompressed_size,
                                     &consumed, &info) ||
        consumed != (size_t)span || (pd && xx_pd_is_stopped(pd)) ||
        (destination &&
         !xx_lzpis2_write_all(destination, decoded,
                               (size_t)info.uncompressed_size, pd))) {
        goto cleanup;
    }

    context->uncompressed_size = info.uncompressed_size;
    context->stream_size = span;
    context->chunk_count = info.chunk_count;
    result = true;
cleanup:
    xx_mem_free(decoded);
    xx_mem_free(input);
    return result;
}

static bool xx_lzpis2_copy_options(xx_list_s *target,
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

static const xx_var *xx_lzpis2_get_option(const xx_list_s *options,
                                           uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_lzpis2_set_record(Abstractformat *self,
                                  xx_archive_record *record) {
    const xx_lzpis2 *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= 0) {
        return false;
    }
    archive = (const xx_lzpis2 *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 6;
    record->data_offset = self->base_address;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_LZPIS2_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_lzpis2_init(xx_lzpis2 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LZPIS2;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzpis2");
    xx_format_set_extension(&archive->format, "lzpis2");
    archive->format.check_is_valid = xx_lzpis2_check_is_valid;
    archive->format.handle_base_info = xx_lzpis2_handle_base_info;
    archive->format.get_format_size = xx_lzpis2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzpis2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzpis2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzpis2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzpis2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzpis2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzpis2_free_archive_records_reading;
    archive->format.destroy = xx_lzpis2_vtable_destroy;
}

xx_lzpis2 *xx_lzpis2_create(xx_io_device *device, int64_t base_address) {
    xx_lzpis2 *archive = (xx_lzpis2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lzpis2_init(archive, device, base_address);
    return archive;
}

void xx_lzpis2_destroy(xx_lzpis2 *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->chunk_count = 0U;
}

static void xx_lzpis2_vtable_destroy(Abstractformat *self) {
    xx_lzpis2_destroy((xx_lzpis2 *)self);
}

void xx_lzpis2_free(xx_lzpis2 *archive) {
    if (!archive) return;
    xx_lzpis2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lzpis2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzpis2_context context;
    return xx_lzpis2_process(self, NULL, &context, pd);
}

bool xx_lzpis2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzpis2_context context;
    xx_lzpis2 *archive;
    if (!self || !xx_lzpis2_process(self, NULL, &context, pd)) {
        if (self) {
            archive = (xx_lzpis2 *)self;
            archive->uncompressed_size = 0U;
            archive->chunk_count = 0U;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }

    archive = (xx_lzpis2 *)self;
    archive->uncompressed_size = context.uncompressed_size;
    archive->chunk_count = context.chunk_count;
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_LZPIS2;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_lzpis2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_lzpis2_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_lzpis2_unpack_to_device(xx_lzpis2 *archive,
                                xx_io_device *destination,
                                xx_pd_struct *pd) {
    xx_lzpis2_context context;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_lzpis2_process(&archive->format, destination, &context, pd)) {
        return false;
    }
    return context.stream_size == archive->format.format_size &&
           context.uncompressed_size == archive->uncompressed_size &&
           context.chunk_count == archive->chunk_count;
}

xx_archive_record_state *xx_lzpis2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_lzpis2_copy_options(&state->options, options) ||
        !xx_lzpis2_set_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_lzpis2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_lzpis2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_lzpis2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    xx_lzpis2 *archive = (xx_lzpis2 *)self;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_option = xx_lzpis2_get_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        xx_lzpis2_context context;
        return xx_lzpis2_process(self, NULL, &context, pd) &&
               context.stream_size == self->format_size &&
               context.uncompressed_size == archive->uncompressed_size;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) return false;
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", XX_LZPIS2_PAYLOAD_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_LZPIS2_PAYLOAD_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path ||
        !xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        result = output && xx_lzpis2_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_lzpis2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_lzpis2_get_uncompressed_size(const xx_lzpis2 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

uint32_t xx_lzpis2_get_chunk_count(const xx_lzpis2 *archive) {
    return archive ? archive->chunk_count : 0U;
}
