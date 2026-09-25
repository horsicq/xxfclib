/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/packimg/xx_packimg.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_PACKIMG exists in the enum. */
#ifdef PACKIMG
#define XX_PACKIMG_FILE_TYPE XX_FILE_TYPE_PACKIMG
#else
#define XX_PACKIMG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The single payload record this container ever publishes. */
#define XX_PACKIMG_MEMBER_NAME "packimg.bin"

typedef struct xx_packimg_private_s {
    int64_t input_size;
    int64_t header_offset;
    int64_t data_offset;
    int64_t archive_end;
    uint32_t data_size;
    uint32_t unknown;
    size_t count;
} xx_packimg_private;

typedef struct xx_packimg_archive_stream_s {
    xx_packimg_private parsed;
    size_t index;
} xx_packimg_archive_stream;

static void xx_packimg_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64 and a PACKIMG
 * tag is normally found deep inside a flash dump, not at offset zero. */
static bool xx_packimg_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_packimg_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_packimg_range_within(int64_t total_size, int64_t offset,
                                    int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_packimg_private_cleanup(xx_packimg_private *parsed) {
    if (!parsed) return;
    /* Nothing here owns heap memory; the cleanup exists for symmetry with the
     * other readers and to leave a failed parse in a defined state. */
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->header_offset = -1;
    parsed->data_offset = -1;
    parsed->archive_end = -1;
}

/*
 * There is no checksum anywhere in a PACKIMG header, so validation rests
 * entirely on the twelve byte ASCII tag plus the requirement that the
 * declared payload is non-empty and physically present.  That is weaker than
 * the other four readers here, and it is the reason the payload size is
 * checked against the device rather than merely recorded.
 */
static bool xx_packimg_parse(Abstractformat *self, xx_packimg_private *parsed,
                             xx_pd_struct *pd) {
    uint8_t header[XX_PACKIMG_HEADER_SIZE];
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->header_offset = -1;
        parsed->data_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->header_offset = self->base_address;
    if (!xx_packimg_range_within(parsed->input_size, self->base_address,
                                 XX_PACKIMG_HEADER_SIZE) ||
        !xx_packimg_read_at(self->device, self->base_address, header,
                            sizeof(header)) ||
        xx_rt_memcmp(header, XX_PACKIMG_TAG, XX_PACKIMG_TAG_SIZE) != 0) {
        goto fail;
    }
    parsed->unknown = xx_data_get_u32(header, sizeof(header), 0x0CU, false);
    /* The size word is little endian, unlike the D-Link and NETGEAR
     * containers; binwalk's structures/packimg.rs reads it that way and every
     * sample examined agrees. */
    parsed->data_size =
        xx_data_get_u32(header, sizeof(header), XX_PACKIMG_SIZE_OFFSET, false);
    if (parsed->data_size == 0U) goto fail;
    if (!xx_packimg_add(self->base_address, XX_PACKIMG_HEADER_SIZE,
                        &parsed->data_offset) ||
        !xx_packimg_range_within(parsed->input_size, parsed->data_offset,
                                 (int64_t)parsed->data_size) ||
        !xx_packimg_add(parsed->data_offset, parsed->data_size,
                        &parsed->archive_end)) {
        goto fail;
    }
    parsed->count = 1U;
    return true;
fail:
    xx_packimg_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_packimg_copy_options(xx_list_s *destination,
                                    const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *xx_packimg_find_option(const xx_list_s *options,
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

static bool xx_packimg_populate_record(xx_archive_record *record,
                                       const xx_packimg_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->header_offset;
    record->header_size = XX_PACKIMG_HEADER_SIZE;
    record->data_offset = parsed->data_offset;
    record->compressed_size = (int64_t)parsed->data_size;
    /* The name is a literal chosen here, never taken from the file, so it
     * needs no sanitising before use as a destination path component. */
    return xx_archive_record_set_original_name(record,
                                               XX_PACKIMG_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_packimg_archive_stream_free(void *pointer) {
    xx_packimg_archive_stream *stream = (xx_packimg_archive_stream *)pointer;
    if (!stream) return;
    xx_packimg_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_packimg_init(xx_packimg *packimg, xx_io_device *dev,
                     int64_t base_address) {
    if (!packimg) return;
    xx_mem_zero(packimg, sizeof(*packimg));
    xx_format_init(&packimg->format, dev, base_address);
    packimg->format.endian = XX_ENDIAN_LITTLE;
    packimg->format.file_type = XX_PACKIMG_FILE_TYPE;
    packimg->format.format_type = XX_TYPE_ARCHIVE;
    packimg->format.is_archive = true;
    xx_format_set_mime_type(&packimg->format,
                            "application/x-packimg-firmware");
    xx_format_set_extension(&packimg->format, "bin");
    packimg->format.check_is_valid = xx_packimg_check_is_valid;
    packimg->format.handle_base_info = xx_packimg_handle_base_info;
    packimg->format.get_format_size = xx_packimg_get_format_size;
    packimg->format.get_number_of_archive_records =
        xx_packimg_get_number_of_archive_records;
    packimg->format.create_archive_records_reading =
        xx_packimg_create_archive_records_reading;
    packimg->format.get_current_archive_record =
        xx_packimg_get_current_archive_record;
    packimg->format.unpack_current_archive_record =
        xx_packimg_unpack_current_archive_record;
    packimg->format.archive_record_move_to_next =
        xx_packimg_archive_record_move_to_next;
    packimg->format.free_archive_records_reading =
        xx_packimg_free_archive_records_reading;
    packimg->format.destroy = xx_packimg_vtable_destroy;
    packimg->data_offset = -1;
    packimg->archive_end = -1;
}

xx_packimg *xx_packimg_create(xx_io_device *dev, int64_t base_address) {
    xx_packimg *packimg = (xx_packimg *)xx_mem_alloc(sizeof(*packimg));
    if (packimg) xx_packimg_init(packimg, dev, base_address);
    return packimg;
}

void xx_packimg_destroy(xx_packimg *packimg) {
    if (!packimg) return;
    if (packimg->internal) {
        xx_packimg_private_cleanup((xx_packimg_private *)packimg->internal);
        xx_mem_free(packimg->internal);
        packimg->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&packimg->format);
}

static void xx_packimg_vtable_destroy(Abstractformat *self) {
    xx_packimg_destroy((xx_packimg *)self);
}

void xx_packimg_free(xx_packimg *packimg) {
    if (!packimg) return;
    xx_packimg_destroy(packimg);
    xx_mem_free(packimg);
}

bool xx_packimg_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_packimg_private parsed;
    bool result = xx_packimg_parse(self, &parsed, pd);
    xx_packimg_private_cleanup(&parsed);
    return result;
}

bool xx_packimg_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_packimg_private *parsed;
    xx_packimg *packimg = (xx_packimg *)self;
    int64_t total_size;
    if (!self || !packimg) return false;
    parsed = (xx_packimg_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_packimg_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (packimg->internal) {
        xx_packimg_private_cleanup((xx_packimg_private *)packimg->internal);
        xx_mem_free(packimg->internal);
    }
    packimg->internal = parsed;
    packimg->number_of_records = parsed->count;
    packimg->number_of_members = parsed->count;
    packimg->data_size = parsed->data_size;
    packimg->unknown = parsed->unknown;
    packimg->data_offset = parsed->data_offset;
    packimg->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_packimg_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_packimg_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_packimg *)self)->number_of_records;
}

xx_archive_record_state *xx_packimg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_packimg_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_packimg_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_packimg_copy_options(&state->options, options) ||
        !xx_packimg_parse(self, &stream->parsed, pd)) {
        xx_packimg_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_packimg_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_packimg_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_packimg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_packimg_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_packimg_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_packimg_archive_stream *)state->internal_state;
    ++stream->index;
    /* One payload only, so the first move always ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_packimg_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_packimg_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_packimg_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_packimg_get_number_of_records(const xx_packimg *packimg) {
    return packimg ? packimg->number_of_records : 0U;
}
uint64_t xx_packimg_get_number_of_members(const xx_packimg *packimg) {
    return packimg ? packimg->number_of_members : 0U;
}
uint32_t xx_packimg_get_data_size(const xx_packimg *packimg) {
    return packimg ? packimg->data_size : 0U;
}
int64_t xx_packimg_get_archive_end(const xx_packimg *packimg) {
    return packimg ? packimg->archive_end : -1;
}
