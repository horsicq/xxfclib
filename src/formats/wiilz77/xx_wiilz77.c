/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for Nintendo LZ10/LZ11 streams, including their common ``LZ77``
 * tag and the optional IMD5 envelope used by Wii assets.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wiilz77/xx_wiilz77.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_WIILZ77_PAYLOAD_NAME "payload"
#define XX_WIILZ77_IMD5_SIZE 32U
#define XX_WIILZ77_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_WIILZ77_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)
#define XX_WIILZ77_MAX_TRAILING_TAGGED 16U
#define XX_WIILZ77_MAX_TRAILING_BARE 10U

typedef struct xx_wiilz77_context_s {
    xx_wiilz77_header header;
    uint32_t data_offset;
    uint32_t imd5_declared_size;
    bool has_imd5_wrapper;
    int64_t stream_size;
} xx_wiilz77_context;

static void xx_wiilz77_vtable_destroy(Abstractformat *self);

static uint32_t xx_wiilz77_read32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static bool xx_wiilz77_read_exact_at(xx_io_device *device, int64_t offset,
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

static bool xx_wiilz77_write_all(xx_io_device *device, const void *data,
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

static bool xx_wiilz77_is_bare_padding(const uint8_t *data, size_t size) {
    size_t index = 0U;
    if (!data) return size == 0U;
    /* nlzss11 can emit an otherwise-unused 0xff group flag at EOF. */
    if (size != 0U && data[0] == 0xffU) index = 1U;
    for (; index < size; ++index) {
        if (data[index] != 0U) return false;
    }
    return true;
}

static bool xx_wiilz77_parse_member(const uint8_t *input, size_t input_size,
                                    xx_wiilz77_context *context) {
    size_t lz_offset = 0U;
    size_t used_size = 0U;
    size_t trailing_size;
    size_t packed_size;
    uint8_t *output = NULL;
    uint64_t max_ratio;
    bool result = false;
    if (!input || !context || input_size < 6U) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    if (input_size >= XX_WIILZ77_IMD5_SIZE + 8U &&
        xx_rt_memcmp(input, "IMD5", 4U) == 0) {
        lz_offset = XX_WIILZ77_IMD5_SIZE;
        context->has_imd5_wrapper = true;
        context->imd5_declared_size = xx_wiilz77_read32be(input + 4U);
    }
    if (lz_offset > input_size ||
        !xx_wiilz77_parse_header(input + lz_offset, input_size - lz_offset,
                                  &context->header) ||
        (context->has_imd5_wrapper && !context->header.has_tag) ||
        context->header.uncompressed_size > XX_WIILZ77_MAX_OUTPUT ||
        context->header.uncompressed_size > (uint64_t)SIZE_MAX ||
        context->header.header_size > input_size - lz_offset) {
        return false;
    }
    packed_size = input_size - lz_offset - context->header.header_size;
    if (packed_size < 2U ||
        (!context->header.has_tag && !context->has_imd5_wrapper &&
         context->header.uncompressed_size < 16U)) {
        return false;
    }
    max_ratio = context->header.variant == XX_WIILZ77_VARIANT_LZ11 ?
                    UINT64_C(16384) : UINT64_C(9);
    if (context->header.uncompressed_size >
        (uint64_t)packed_size * max_ratio + UINT64_C(64)) {
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)context->header.uncompressed_size);
    if (!output ||
        !xx_wiilz77_decompress_memory(input + lz_offset,
                                      input_size - lz_offset, output,
                                      (size_t)context->header.uncompressed_size,
                                      &used_size) ||
        used_size < context->header.header_size + 2U ||
        used_size > input_size - lz_offset) {
        goto cleanup;
    }
    trailing_size = input_size - lz_offset - used_size;
    if (context->header.has_tag || context->has_imd5_wrapper) {
        if (trailing_size > XX_WIILZ77_MAX_TRAILING_TAGGED) goto cleanup;
    } else if (trailing_size > XX_WIILZ77_MAX_TRAILING_BARE ||
               !xx_wiilz77_is_bare_padding(input + lz_offset + used_size,
                                            trailing_size)) {
        goto cleanup;
    }
    if (lz_offset + context->header.header_size > UINT32_MAX) goto cleanup;
    context->data_offset = (uint32_t)(lz_offset + context->header.header_size);
    context->stream_size = (int64_t)input_size;
    result = true;
cleanup:
    xx_mem_free(output);
    return result;
}

static bool xx_wiilz77_decode_stream(Abstractformat *self,
                                     xx_io_device *destination,
                                     xx_wiilz77_context *context,
                                     xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    xx_wiilz77_context parsed;
    size_t used_size;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 || !context ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        (uint64_t)(total_size - self->base_address) > XX_WIILZ77_MAX_INPUT ||
        (uint64_t)(total_size - self->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    input_size = total_size - self->base_address;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input || !xx_wiilz77_read_exact_at(self->device, self->base_address,
                                            input, (size_t)input_size) ||
        !xx_wiilz77_parse_member(input, (size_t)input_size, &parsed)) {
        goto cleanup;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)parsed.header.uncompressed_size);
    if (!output ||
        !xx_wiilz77_decompress_memory(
            input + (parsed.has_imd5_wrapper ? XX_WIILZ77_IMD5_SIZE : 0U),
            (size_t)input_size -
                (parsed.has_imd5_wrapper ? XX_WIILZ77_IMD5_SIZE : 0U), output,
            (size_t)parsed.header.uncompressed_size, &used_size) ||
        (pd && xx_pd_is_stopped(pd)) ||
        (destination && !xx_wiilz77_write_all(
                            destination, output,
                            (size_t)parsed.header.uncompressed_size, pd))) {
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

static bool xx_wiilz77_copy_options(xx_list_s *destination,
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

static const xx_var *xx_wiilz77_find_option(const xx_list_s *options,
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

static bool xx_wiilz77_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_wiilz77 *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 6) {
        return false;
    }
    archive = (const xx_wiilz77 *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = archive->data_offset;
    record->data_offset = self->base_address + archive->data_offset;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_WIILZ77_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_wiilz77_init(xx_wiilz77 *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_WII_LZ77;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-nintendo-lz77");
    xx_format_set_extension(&archive->format, "lz77");
    archive->format.check_is_valid = xx_wiilz77_check_is_valid;
    archive->format.handle_base_info = xx_wiilz77_handle_base_info;
    archive->format.get_format_size = xx_wiilz77_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wiilz77_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wiilz77_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wiilz77_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wiilz77_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wiilz77_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wiilz77_free_archive_records_reading;
    archive->format.destroy = xx_wiilz77_vtable_destroy;
    archive->stream_end = -1;
}

xx_wiilz77 *xx_wiilz77_create(xx_io_device *device, int64_t base_address) {
    xx_wiilz77 *archive = (xx_wiilz77 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_wiilz77_init(archive, device, base_address);
    return archive;
}

void xx_wiilz77_destroy(xx_wiilz77 *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->data_offset = 0U;
    archive->imd5_declared_size = 0U;
    archive->variant = XX_WIILZ77_VARIANT_UNKNOWN;
    archive->has_tag = false;
    archive->has_imd5_wrapper = false;
}

static void xx_wiilz77_vtable_destroy(Abstractformat *self) {
    xx_wiilz77_destroy((xx_wiilz77 *)self);
}

void xx_wiilz77_free(xx_wiilz77 *archive) {
    if (!archive) return;
    xx_wiilz77_destroy(archive);
    xx_mem_free(archive);
}

bool xx_wiilz77_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wiilz77_context context;
    return xx_wiilz77_decode_stream(self, NULL, &context, pd);
}

bool xx_wiilz77_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wiilz77_context context;
    xx_wiilz77 *archive;
    if (!self || !xx_wiilz77_decode_stream(self, NULL, &context, pd)) {
        if (self) {
            archive = (xx_wiilz77 *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            archive->data_offset = 0U;
            archive->imd5_declared_size = 0U;
            archive->variant = XX_WIILZ77_VARIANT_UNKNOWN;
            archive->has_tag = false;
            archive->has_imd5_wrapper = false;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_wiilz77 *)self;
    archive->uncompressed_size = context.header.uncompressed_size;
    archive->stream_end = self->base_address + context.stream_size;
    archive->data_offset = context.data_offset;
    archive->imd5_declared_size = context.imd5_declared_size;
    archive->variant = context.header.variant;
    archive->has_tag = context.header.has_tag;
    archive->has_imd5_wrapper = context.has_imd5_wrapper;
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_WII_LZ77;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_wiilz77_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_wiilz77_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_wiilz77_unpack_to_device(xx_wiilz77 *archive,
                                 xx_io_device *destination,
                                 xx_pd_struct *pd) {
    xx_wiilz77_context context;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_wiilz77_decode_stream(&archive->format, destination, &context,
                                  pd)) {
        return false;
    }
    return context.stream_size == archive->format.format_size &&
           context.header.uncompressed_size == archive->uncompressed_size &&
           context.data_offset == archive->data_offset &&
           context.header.variant == archive->variant &&
           context.header.has_tag == archive->has_tag &&
           context.has_imd5_wrapper == archive->has_imd5_wrapper;
}

xx_archive_record_state *xx_wiilz77_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_wiilz77_copy_options(&state->options, options) ||
        !xx_wiilz77_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_wiilz77_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_wiilz77_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_wiilz77_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_wiilz77 *archive = (xx_wiilz77 *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_wiilz77_find_option(&state->options,
                                        XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_wiilz77_context context;
        return xx_wiilz77_decode_stream(self, NULL, &context, pd) &&
               context.stream_size == self->format_size &&
               context.header.uncompressed_size == archive->uncompressed_size &&
               context.header.variant == archive->variant;
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
                                          XX_WIILZ77_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_WIILZ77_PAYLOAD_NAME);
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
        result = output && xx_wiilz77_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_wiilz77_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_wiilz77_get_uncompressed_size(const xx_wiilz77 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_wiilz77_get_stream_end(const xx_wiilz77 *archive) {
    return archive ? archive->stream_end : -1;
}

xx_wiilz77_variant_t xx_wiilz77_get_variant(const xx_wiilz77 *archive) {
    return archive ? archive->variant : XX_WIILZ77_VARIANT_UNKNOWN;
}

bool xx_wiilz77_has_tag(const xx_wiilz77 *archive) {
    return archive && archive->has_tag;
}

bool xx_wiilz77_has_imd5_wrapper(const xx_wiilz77 *archive) {
    return archive && archive->has_imd5_wrapper;
}
