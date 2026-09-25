/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rtk/xx_rtk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_RTK exists in the enum. */
#ifdef RTK
#define XX_RTK_FILE_TYPE XX_FILE_TYPE_RTK
#else
#define XX_RTK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_RTK_HEADER_SIZE 32
#define XX_RTK_MAGIC_SIZE 4
#define XX_RTK_PAYLOAD_NAME "rtk_image.bin"

typedef struct xx_rtk_private_s {
    int64_t input_size;
    int64_t payload_offset;
    int64_t payload_size;
    int64_t archive_end;
    uint32_t image_size;
    uint32_t checksum;
    uint32_t header_size;
    uint32_t identifier;
} xx_rtk_private;

typedef struct xx_rtk_archive_stream_s {
    xx_rtk_private parsed;
    size_t index;
} xx_rtk_archive_stream;

static void xx_rtk_vtable_destroy(Abstractformat *self);

/* seek64 rather than seek: the payload offset comes straight out of the
 * header and can name a position past the 2 GiB `long` ceiling on Win64. */
static bool xx_rtk_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_rtk_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

static bool xx_rtk_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_rtk_private_cleanup(xx_rtk_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->payload_offset = -1;
    parsed->archive_end = -1;
}

static bool xx_rtk_parse(Abstractformat *self, xx_rtk_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_RTK_HEADER_SIZE];
    int64_t total_size;
    int64_t effective_header;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->payload_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_rtk_range_within(total_size, self->base_address,
                             XX_RTK_HEADER_SIZE) ||
        !xx_rtk_read_at(self->device, self->base_address, header,
                        sizeof(header)) ||
        xx_rt_memcmp(header, "RTK0", XX_RTK_MAGIC_SIZE) != 0) {
        return false;
    }
    parsed->image_size = xx_data_get_u32(header, sizeof(header), 4U, false);
    parsed->checksum = xx_data_get_u32(header, sizeof(header), 8U, false);
    parsed->header_size = xx_data_get_u32(header, sizeof(header), 16U, false);
    parsed->identifier = xx_data_get_u32(header, sizeof(header), 28U, false);
    /* The stored header size excludes the four magic bytes. */
    effective_header = (int64_t)parsed->header_size + XX_RTK_MAGIC_SIZE;
    if (effective_header < XX_RTK_HEADER_SIZE) return false;
    if (!xx_rtk_add(self->base_address, (uint64_t)effective_header,
                    &parsed->payload_offset) ||
        !xx_rtk_range_within(total_size, parsed->payload_offset,
                             (int64_t)parsed->image_size)) {
        parsed->payload_offset = -1;
        return false;
    }
    parsed->input_size = total_size;
    parsed->payload_size = (int64_t)parsed->image_size;
    parsed->archive_end = parsed->payload_offset + parsed->payload_size;
    return true;
}

static bool xx_rtk_copy_options(xx_list_s *destination,
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

static const xx_var *xx_rtk_find_option(const xx_list_s *options,
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

static bool xx_rtk_populate_record(xx_archive_record *record,
                                   const xx_rtk_private *parsed) {
    if (!record || !parsed || parsed->payload_offset < 0) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->payload_offset -
                            ((int64_t)parsed->header_size + XX_RTK_MAGIC_SIZE);
    record->header_size = (int64_t)parsed->header_size + XX_RTK_MAGIC_SIZE;
    record->data_offset = parsed->payload_offset;
    record->compressed_size = parsed->payload_size;
    return xx_archive_record_set_original_name(record, XX_RTK_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_rtk_archive_stream_free(void *pointer) {
    xx_rtk_archive_stream *stream = (xx_rtk_archive_stream *)pointer;
    if (!stream) return;
    xx_rtk_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_rtk_init(xx_rtk *rtk, xx_io_device *dev, int64_t base_address) {
    if (!rtk) return;
    xx_mem_zero(rtk, sizeof(*rtk));
    xx_format_init(&rtk->format, dev, base_address);
    rtk->format.endian = XX_ENDIAN_LITTLE;
    rtk->format.file_type = XX_RTK_FILE_TYPE;
    rtk->format.format_type = XX_TYPE_ARCHIVE;
    rtk->format.is_archive = true;
    xx_format_set_mime_type(&rtk->format, "application/x-rtk-firmware");
    xx_format_set_extension(&rtk->format, "bin");
    rtk->format.check_is_valid = xx_rtk_check_is_valid;
    rtk->format.handle_base_info = xx_rtk_handle_base_info;
    rtk->format.get_format_size = xx_rtk_get_format_size;
    rtk->format.get_number_of_archive_records =
        xx_rtk_get_number_of_archive_records;
    rtk->format.create_archive_records_reading =
        xx_rtk_create_archive_records_reading;
    rtk->format.get_current_archive_record =
        xx_rtk_get_current_archive_record;
    rtk->format.unpack_current_archive_record =
        xx_rtk_unpack_current_archive_record;
    rtk->format.archive_record_move_to_next =
        xx_rtk_archive_record_move_to_next;
    rtk->format.free_archive_records_reading =
        xx_rtk_free_archive_records_reading;
    rtk->format.destroy = xx_rtk_vtable_destroy;
    rtk->payload_offset = -1;
    rtk->archive_end = -1;
}

xx_rtk *xx_rtk_create(xx_io_device *dev, int64_t base_address) {
    xx_rtk *rtk = (xx_rtk *)xx_mem_alloc(sizeof(*rtk));
    if (rtk) xx_rtk_init(rtk, dev, base_address);
    return rtk;
}

void xx_rtk_destroy(xx_rtk *rtk) {
    if (!rtk) return;
    if (rtk->internal) {
        xx_rtk_private_cleanup((xx_rtk_private *)rtk->internal);
        xx_mem_free(rtk->internal);
        rtk->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&rtk->format);
}

static void xx_rtk_vtable_destroy(Abstractformat *self) {
    xx_rtk_destroy((xx_rtk *)self);
}

void xx_rtk_free(xx_rtk *rtk) {
    if (!rtk) return;
    xx_rtk_destroy(rtk);
    xx_mem_free(rtk);
}

bool xx_rtk_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rtk_private parsed;
    bool result = xx_rtk_parse(self, &parsed, pd);
    xx_rtk_private_cleanup(&parsed);
    return result;
}

bool xx_rtk_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rtk_private *parsed;
    xx_rtk *rtk = (xx_rtk *)self;
    int64_t total_size;
    if (!self || !rtk) return false;
    parsed = (xx_rtk_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_rtk_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (rtk->internal) {
        xx_rtk_private_cleanup((xx_rtk_private *)rtk->internal);
        xx_mem_free(rtk->internal);
    }
    rtk->internal = parsed;
    rtk->number_of_records = parsed->payload_size > 0 ? 1U : 0U;
    rtk->number_of_members = rtk->number_of_records;
    rtk->image_size = parsed->image_size;
    rtk->checksum = parsed->checksum;
    rtk->header_size = parsed->header_size;
    rtk->identifier = parsed->identifier;
    rtk->payload_offset = parsed->payload_offset;
    rtk->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = rtk->number_of_records;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_rtk_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_rtk_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_rtk *)self)->number_of_records;
}

xx_archive_record_state *xx_rtk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_rtk_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_rtk_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_rtk_copy_options(&state->options, options) ||
        !xx_rtk_parse(self, &stream->parsed, pd)) {
        xx_rtk_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_rtk_archive_stream_free;
    state->total_records = stream->parsed.payload_size > 0 ? 1 : 0;
    if (stream->parsed.payload_size > 0 &&
        xx_rtk_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_rtk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rtk_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_rtk_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_rtk_archive_stream *)state->internal_state;
    /* One payload per image, so the first advance ends the enumeration. */
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_rtk_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_rtk_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_rtk_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_rtk_get_number_of_records(const xx_rtk *rtk) {
    return rtk ? rtk->number_of_records : 0U;
}
uint32_t xx_rtk_get_image_size(const xx_rtk *rtk) {
    return rtk ? rtk->image_size : 0U;
}
uint32_t xx_rtk_get_checksum(const xx_rtk *rtk) {
    return rtk ? rtk->checksum : 0U;
}
uint32_t xx_rtk_get_header_size(const xx_rtk *rtk) {
    return rtk ? rtk->header_size : 0U;
}
uint32_t xx_rtk_get_identifier(const xx_rtk *rtk) {
    return rtk ? rtk->identifier : 0U;
}
int64_t xx_rtk_get_payload_offset(const xx_rtk *rtk) {
    return rtk ? rtk->payload_offset : -1;
}
int64_t xx_rtk_get_archive_end(const xx_rtk *rtk) {
    return rtk ? rtk->archive_end : -1;
}
