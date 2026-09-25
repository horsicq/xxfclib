/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/oraclesqueeze/xx_oraclesqueeze.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#define XX_ORACLESQUEEZE_FIXED_HEADER_SIZE 8U
#define XX_ORACLESQUEEZE_MAX_NAME 255U
#define XX_ORACLESQUEEZE_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_ORACLESQUEEZE_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)

typedef struct xx_oraclesqueeze_context_s {
    uint64_t uncompressed_size;
    uint32_t tree_offset;
    uint16_t checksum;
    int64_t stream_size;
    char file_name[256];
} xx_oraclesqueeze_context;

static void xx_oraclesqueeze_vtable_destroy(Abstractformat *self);

static uint16_t xx_oraclesqueeze_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_oraclesqueeze_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_oraclesqueeze_read_exact_at(xx_io_device *device,
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

static bool xx_oraclesqueeze_write_all(xx_io_device *device, const void *data,
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

static bool xx_oraclesqueeze_find_name(const uint8_t *input, size_t input_size,
                                       size_t offset, char *name,
                                       size_t *next_offset) {
    size_t length = 0U;
    size_t index;
    if (!input || !name || !next_offset || offset >= input_size) return false;
    while (offset + length < input_size && length < XX_ORACLESQUEEZE_MAX_NAME &&
           input[offset + length] != 0U) {
        uint8_t ch = input[offset + length];
        if (ch < 0x20U || ch == 0x7fU) return false;
        ++length;
    }
    if (length == 0U || offset + length >= input_size ||
        input[offset + length] != 0U) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t ch = input[offset + index];
        bool safe = (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                    ch == '-';
        name[index] = safe ? (char)ch : '_';
    }
    name[length] = '\0';
    if (xx_rt_strcmp(name, ".") == 0 || xx_rt_strcmp(name, "..") == 0) {
        xx_rt_memcpy(name, "payload", sizeof("payload"));
    }
    *next_offset = offset + length + 1U;
    return true;
}

static bool xx_oraclesqueeze_tree_at(const uint8_t *input, size_t input_size,
                                     size_t offset, size_t *end_offset) {
    xx_oraclesqueeze_tree_info info;
    if (!input || !end_offset || offset >= input_size ||
        !xx_oraclesqueeze_parse_tree(input + offset, input_size - offset,
                                     &info) ||
        info.bitstream_offset > input_size - offset) {
        return false;
    }
    *end_offset = offset + info.bitstream_offset;
    return true;
}

/* Plain CP/M Squeeze stores the filename at byte four.  Do not claim a file
 * that already has that structurally valid layout merely because its first
 * four name bytes also form a plausible Oracle decoded-length field. */
static bool xx_oraclesqueeze_looks_like_classic(const uint8_t *input,
                                                 size_t input_size) {
    char unused_name[256];
    size_t tree_offset;
    size_t end_offset;
    return xx_oraclesqueeze_find_name(input, input_size, 4U, unused_name,
                                      &tree_offset) &&
           xx_oraclesqueeze_tree_at(input, input_size, tree_offset,
                                    &end_offset);
}

static bool xx_oraclesqueeze_parse_buffer(const uint8_t *input,
                                          size_t input_size,
                                          xx_oraclesqueeze_context *context) {
    uint64_t uncompressed_size;
    size_t tree_offset;
    size_t end_offset;
    if (!input || !context || input_size < 16U || input[0] != 0x76U ||
        input[1] != 0xffU || xx_oraclesqueeze_looks_like_classic(input,
                                                                   input_size)) {
        return false;
    }
    uncompressed_size = xx_oraclesqueeze_read32le(input + 2U);
    if (uncompressed_size == 0U ||
        uncompressed_size > XX_ORACLESQUEEZE_MAX_OUTPUT ||
        uncompressed_size > (uint64_t)SIZE_MAX ||
        !xx_oraclesqueeze_find_name(input, input_size,
                                    XX_ORACLESQUEEZE_FIXED_HEADER_SIZE,
                                    context->file_name, &tree_offset) ||
        !xx_oraclesqueeze_tree_at(input, input_size, tree_offset,
                                  &end_offset) || tree_offset > UINT32_MAX) {
        return false;
    }
    (void)end_offset;
    context->uncompressed_size = uncompressed_size;
    context->tree_offset = (uint32_t)tree_offset;
    context->checksum = xx_oraclesqueeze_read16le(input + 6U);
    context->stream_size = (int64_t)input_size;
    return true;
}

static bool xx_oraclesqueeze_decode_stream(Abstractformat *self,
                                            xx_io_device *destination,
                                            xx_oraclesqueeze_context *context,
                                            xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    xx_oraclesqueeze_context parsed;
    uint16_t checksum = 0U;
    size_t used_size = 0U;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 || !context ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        (uint64_t)(total_size - self->base_address) > XX_ORACLESQUEEZE_MAX_INPUT ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_oraclesqueeze_read_exact_at(self->device,
                                                  self->base_address, input,
                                                  (size_t)input_size) ||
        !xx_oraclesqueeze_parse_buffer(input, (size_t)input_size, &parsed)) {
        goto cleanup;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)parsed.uncompressed_size);
    if (!output ||
        !xx_oraclesqueeze_decompress_memory(
            input + parsed.tree_offset, (size_t)input_size - parsed.tree_offset,
            output, (size_t)parsed.uncompressed_size, &used_size, &checksum) ||
        checksum != parsed.checksum || (pd && xx_pd_is_stopped(pd)) ||
        (destination && !xx_oraclesqueeze_write_all(
                            destination, output,
                            (size_t)parsed.uncompressed_size, pd))) {
        goto cleanup;
    }
    (void)used_size;
    *context = parsed;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

static bool xx_oraclesqueeze_copy_options(xx_list_s *destination,
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

static const xx_var *xx_oraclesqueeze_find_option(const xx_list_s *options,
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

static bool xx_oraclesqueeze_populate_record(Abstractformat *self,
                                             xx_archive_record *record) {
    const xx_oraclesqueeze *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 16) {
        return false;
    }
    archive = (const xx_oraclesqueeze *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = archive->tree_offset;
    record->data_offset = self->base_address + archive->tree_offset;
    record->compressed_size = self->format_size - archive->tree_offset;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          archive->file_name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_oraclesqueeze_init(xx_oraclesqueeze *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ORACLE_SQUEEZE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-oracle-squeeze");
    xx_format_set_extension(&archive->format, "sq");
    archive->format.check_is_valid = xx_oraclesqueeze_check_is_valid;
    archive->format.handle_base_info = xx_oraclesqueeze_handle_base_info;
    archive->format.get_format_size = xx_oraclesqueeze_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_oraclesqueeze_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_oraclesqueeze_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_oraclesqueeze_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_oraclesqueeze_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_oraclesqueeze_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_oraclesqueeze_free_archive_records_reading;
    archive->format.destroy = xx_oraclesqueeze_vtable_destroy;
    archive->stream_end = -1;
}

xx_oraclesqueeze *xx_oraclesqueeze_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_oraclesqueeze *archive =
        (xx_oraclesqueeze *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_oraclesqueeze_init(archive, device, base_address);
    return archive;
}

void xx_oraclesqueeze_destroy(xx_oraclesqueeze *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->tree_offset = 0U;
    archive->checksum = 0U;
    archive->file_name[0] = '\0';
}

static void xx_oraclesqueeze_vtable_destroy(Abstractformat *self) {
    xx_oraclesqueeze_destroy((xx_oraclesqueeze *)self);
}

void xx_oraclesqueeze_free(xx_oraclesqueeze *archive) {
    if (!archive) return;
    xx_oraclesqueeze_destroy(archive);
    xx_mem_free(archive);
}

bool xx_oraclesqueeze_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_oraclesqueeze_context context;
    return xx_oraclesqueeze_decode_stream(self, NULL, &context, pd);
}

bool xx_oraclesqueeze_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_oraclesqueeze_context context;
    xx_oraclesqueeze *archive;
    if (!self || !xx_oraclesqueeze_decode_stream(self, NULL, &context, pd)) {
        if (self) {
            archive = (xx_oraclesqueeze *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            archive->tree_offset = 0U;
            archive->checksum = 0U;
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
    archive = (xx_oraclesqueeze *)self;
    archive->uncompressed_size = context.uncompressed_size;
    archive->stream_end = self->base_address + context.stream_size;
    archive->tree_offset = context.tree_offset;
    archive->checksum = context.checksum;
    xx_rt_memcpy(archive->file_name, context.file_name, sizeof(archive->file_name));
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_ORACLE_SQUEEZE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_oraclesqueeze_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_oraclesqueeze_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_oraclesqueeze_unpack_to_device(xx_oraclesqueeze *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd) {
    xx_oraclesqueeze_context context;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_oraclesqueeze_decode_stream(&archive->format, destination,
                                        &context, pd)) {
        return false;
    }
    return context.stream_size == archive->format.format_size &&
           context.uncompressed_size == archive->uncompressed_size &&
           context.tree_offset == archive->tree_offset &&
           context.checksum == archive->checksum &&
           xx_rt_strcmp(context.file_name, archive->file_name) == 0;
}

xx_archive_record_state *xx_oraclesqueeze_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_oraclesqueeze_copy_options(&state->options, options) ||
        !xx_oraclesqueeze_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_oraclesqueeze_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_oraclesqueeze_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_oraclesqueeze_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_oraclesqueeze *archive = (xx_oraclesqueeze *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_oraclesqueeze_find_option(&state->options,
                                              XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_oraclesqueeze_context context;
        return xx_oraclesqueeze_decode_stream(self, NULL, &context, pd) &&
               context.stream_size == self->format_size &&
               context.uncompressed_size == archive->uncompressed_size &&
               context.checksum == archive->checksum;
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
        created = output != NULL;
        result = output && xx_oraclesqueeze_unpack_to_device(archive, output,
                                                              pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_oraclesqueeze_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_oraclesqueeze_get_uncompressed_size(
    const xx_oraclesqueeze *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_oraclesqueeze_get_stream_end(const xx_oraclesqueeze *archive) {
    return archive ? archive->stream_end : -1;
}

uint16_t xx_oraclesqueeze_get_checksum(const xx_oraclesqueeze *archive) {
    return archive ? archive->checksum : 0U;
}
