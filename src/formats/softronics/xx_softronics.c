/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/softronics/xx_softronics.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define XX_SOFTRONICS_SIGNATURE_OFFSET 2U
#define XX_SOFTRONICS_SIGNATURE_SIZE 40U
#define XX_SOFTRONICS_COMPRESSED_SIZE_OFFSET 0x2aU
#define XX_SOFTRONICS_NAME_OFFSET 0x2eU
#define XX_SOFTRONICS_MAX_NAME 12U
#define XX_SOFTRONICS_TAIL_SIZE 8U
#define XX_SOFTRONICS_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_SOFTRONICS_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)

static const char xx_softronics_signature[] =
    "Softronics Compressed File\0Version 2.00\0";

typedef struct xx_softronics_context_s {
    uint64_t uncompressed_size;
    uint32_t header_size;
    uint32_t compressed_size;
    uint32_t dos_datetime;
    int64_t stream_size;
    char file_name[13];
} xx_softronics_context;

static void xx_softronics_vtable_destroy(Abstractformat *self);

static uint32_t xx_softronics_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_softronics_read_exact_at(xx_io_device *device,
                                        int64_t offset, void *data,
                                        size_t size) {
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

static bool xx_softronics_write_all(xx_io_device *device, const void *data,
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

static bool xx_softronics_parse_name(const uint8_t *input, size_t input_size,
                                     char *name, size_t *name_size) {
    size_t index;
    if (!input || !name || !name_size ||
        XX_SOFTRONICS_NAME_OFFSET >= input_size) {
        return false;
    }
    for (index = 0U; index < XX_SOFTRONICS_MAX_NAME; ++index) {
        uint8_t ch;
        bool safe;
        if (XX_SOFTRONICS_NAME_OFFSET + index >= input_size) return false;
        ch = input[XX_SOFTRONICS_NAME_OFFSET + index];
        if (ch == 0U) break;
        if (ch < 0x20U || ch == 0x7fU) return false;
        safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
               ch == '-';
        name[index] = safe ? (char)ch : '_';
    }
    /* A name that runs the full twelve characters is not an unterminated
     * name: 8.3 is exactly 8 + '.' + 3, so twelve is the legal maximum and
     * the terminator check below is what proves termination either way.
     * Rejecting index == MAX_NAME here dropped every member with a full
     * 12-character name - a third of the corpus. */
    if (index == 0U ||
        XX_SOFTRONICS_NAME_OFFSET + index >= input_size ||
        input[XX_SOFTRONICS_NAME_OFFSET + index] != 0U) {
        return false;
    }
    name[index] = '\0';
    if (xx_rt_strcmp(name, ".") == 0 || xx_rt_strcmp(name, "..") == 0) {
        xx_rt_memcpy(name, "payload", sizeof("payload"));
    }
    *name_size = index;
    return true;
}

static bool xx_softronics_parse_buffer(const uint8_t *input,
                                        size_t input_size,
                                        xx_softronics_context *context) {
    size_t name_size;
    uint64_t header_size;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    size_t tail_offset;
    if (!input || !context || input_size < 57U || input[1] != 0U ||
        xx_rt_memcmp(input + XX_SOFTRONICS_SIGNATURE_OFFSET,
               xx_softronics_signature, XX_SOFTRONICS_SIGNATURE_SIZE) != 0) {
        return false;
    }
    compressed_size = xx_softronics_read32le(
        input + XX_SOFTRONICS_COMPRESSED_SIZE_OFFSET);
    if (compressed_size == 0U ||
        !xx_softronics_parse_name(input, input_size, context->file_name,
                                  &name_size)) {
        return false;
    }
    header_size = (uint64_t)XX_SOFTRONICS_NAME_OFFSET + name_size + 1U +
                  XX_SOFTRONICS_TAIL_SIZE;
    if (header_size > UINT32_MAX || header_size > input_size ||
        input[0] != (uint8_t)header_size ||
        (uint64_t)compressed_size != input_size - header_size) {
        return false;
    }
    tail_offset = XX_SOFTRONICS_NAME_OFFSET + name_size + 1U;
    uncompressed_size = xx_softronics_read32le(input + tail_offset);
    if (uncompressed_size == 0U ||
        (uint64_t)uncompressed_size > XX_SOFTRONICS_MAX_OUTPUT ||
        (uint64_t)uncompressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    context->uncompressed_size = uncompressed_size;
    context->header_size = (uint32_t)header_size;
    context->compressed_size = compressed_size;
    context->dos_datetime = xx_softronics_read32le(input + tail_offset + 4U);
    context->stream_size = (int64_t)input_size;
    return true;
}

static bool xx_softronics_decode_stream(Abstractformat *self,
                                        xx_io_device *destination,
                                        xx_softronics_context *context,
                                        xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    xx_softronics_context parsed;
    size_t consumed = 0U;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 || !context ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        (uint64_t)(total_size - self->base_address) > XX_SOFTRONICS_MAX_INPUT ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_softronics_read_exact_at(self->device,
                                                self->base_address, input,
                                                (size_t)input_size) ||
        !xx_softronics_parse_buffer(input, (size_t)input_size, &parsed)) {
        goto cleanup;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)parsed.uncompressed_size);
    if (!output ||
        !xx_softronics_lzw_decompress_memory(
            input + parsed.header_size, parsed.compressed_size, output,
            (size_t)parsed.uncompressed_size, &consumed) ||
        consumed != parsed.compressed_size || (pd && xx_pd_is_stopped(pd)) ||
        (destination && !xx_softronics_write_all(
                            destination, output,
                            (size_t)parsed.uncompressed_size, pd))) {
        goto cleanup;
    }
    *context = parsed;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

static bool xx_softronics_copy_options(xx_list_s *destination,
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

static const xx_var *xx_softronics_find_option(const xx_list_s *options,
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

static bool xx_softronics_populate_record(Abstractformat *self,
                                          xx_archive_record *record) {
    const xx_softronics *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 57) {
        return false;
    }
    archive = (const xx_softronics *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = archive->header_size;
    record->data_offset = self->base_address + archive->header_size;
    record->compressed_size = archive->compressed_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          archive->file_name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          archive->compressed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_softronics_init(xx_softronics *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SOFTRONICS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-softronics-compressed");
    archive->format.check_is_valid = xx_softronics_check_is_valid;
    archive->format.handle_base_info = xx_softronics_handle_base_info;
    archive->format.get_format_size = xx_softronics_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_softronics_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_softronics_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_softronics_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_softronics_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_softronics_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_softronics_free_archive_records_reading;
    archive->format.destroy = xx_softronics_vtable_destroy;
    archive->stream_end = -1;
}

xx_softronics *xx_softronics_create(xx_io_device *device,
                                     int64_t base_address) {
    xx_softronics *archive = (xx_softronics *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_softronics_init(archive, device, base_address);
    return archive;
}

void xx_softronics_destroy(xx_softronics *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->header_size = 0U;
    archive->compressed_size = 0U;
    archive->dos_datetime = 0U;
    archive->file_name[0] = '\0';
}

static void xx_softronics_vtable_destroy(Abstractformat *self) {
    xx_softronics_destroy((xx_softronics *)self);
}

void xx_softronics_free(xx_softronics *archive) {
    if (!archive) return;
    xx_softronics_destroy(archive);
    xx_mem_free(archive);
}

bool xx_softronics_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_softronics_context context;
    return xx_softronics_decode_stream(self, NULL, &context, pd);
}

bool xx_softronics_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_softronics_context context;
    xx_softronics *archive;
    if (!self || !xx_softronics_decode_stream(self, NULL, &context, pd)) {
        if (self) {
            archive = (xx_softronics *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            archive->header_size = 0U;
            archive->compressed_size = 0U;
            archive->dos_datetime = 0U;
            archive->file_name[0] = '\0';
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_softronics *)self;
    archive->uncompressed_size = context.uncompressed_size;
    archive->stream_end = self->base_address + context.stream_size;
    archive->header_size = context.header_size;
    archive->compressed_size = context.compressed_size;
    archive->dos_datetime = context.dos_datetime;
    xx_rt_memcpy(archive->file_name, context.file_name, sizeof(archive->file_name));
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_SOFTRONICS;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_softronics_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_softronics_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_softronics_unpack_to_device(xx_softronics *archive,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    xx_softronics_context context;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_softronics_decode_stream(&archive->format, destination, &context,
                                     pd)) {
        return false;
    }
    return context.stream_size == archive->format.format_size &&
           context.uncompressed_size == archive->uncompressed_size &&
           context.header_size == archive->header_size &&
           context.compressed_size == archive->compressed_size &&
           xx_rt_strcmp(context.file_name, archive->file_name) == 0;
}

xx_archive_record_state *xx_softronics_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_softronics_copy_options(&state->options, options) ||
        !xx_softronics_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_softronics_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_softronics_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_softronics_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    xx_softronics *archive = (xx_softronics *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_softronics_find_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_softronics_context context;
        return xx_softronics_decode_stream(self, NULL, &context, pd) &&
               context.stream_size == self->format_size &&
               context.uncompressed_size == archive->uncompressed_size &&
               context.compressed_size == archive->compressed_size;
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
        destination_path = xx_str_concat3(base_path, "/", archive->file_name);
    } else {
        destination_path = xx_str_concat(base_path, archive->file_name);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path ||
        !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_softronics_unpack_to_device(archive, output,
                                                           pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_softronics_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_softronics_get_uncompressed_size(
    const xx_softronics *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_softronics_get_stream_end(const xx_softronics *archive) {
    return archive ? archive->stream_end : -1;
}
