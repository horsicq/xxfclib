/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/shrs/xx_shrs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_SHRS exists in the enum. */
#ifdef SHRS
#define XX_SHRS_FILE_TYPE XX_FILE_TYPE_SHRS
#else
#define XX_SHRS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Only the leading fields and the IV are read; the rest of the 0x6DC header
 *  has no established layout and is not interpreted. */
#define XX_SHRS_PREFIX_SIZE 28U

typedef struct xx_shrs_region_s {
    const char *name; /**< A literal chosen here, never from the file. */
    int64_t data_offset;
    int64_t data_size;
    bool is_encrypted;
} xx_shrs_region;

typedef struct xx_shrs_private_s {
    xx_shrs_region regions[XX_SHRS_MAX_RECORDS];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    int64_t encrypted_data_offset;
    uint32_t encrypted_data_size;
    uint32_t field_4;
    uint8_t iv[XX_SHRS_IV_SIZE];
} xx_shrs_private;

typedef struct xx_shrs_archive_stream_s {
    xx_shrs_private parsed;
    size_t index;
} xx_shrs_archive_stream;

static void xx_shrs_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: the header fields are 32-bit but the
 * base address inside a larger carrier is not, and long is 32-bit on Win64. */
static bool xx_shrs_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_shrs_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_shrs_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_shrs_private_cleanup(xx_shrs_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->encrypted_data_offset = -1;
}

static void xx_shrs_push(xx_shrs_private *parsed, const char *name,
                         int64_t offset, int64_t size, bool is_encrypted) {
    xx_shrs_region *region;
    if (!parsed || parsed->count >= XX_SHRS_MAX_RECORDS || size <= 0) return;
    region = &parsed->regions[parsed->count++];
    region->name = name;
    region->data_offset = offset;
    region->data_size = size;
    region->is_encrypted = is_encrypted;
}

static bool xx_shrs_parse(Abstractformat *self, xx_shrs_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t prefix[XX_SHRS_PREFIX_SIZE];
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->encrypted_data_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_shrs_range_within(parsed->input_size, self->base_address,
                              XX_SHRS_PREFIX_SIZE) ||
        !xx_shrs_read_at(self->device, self->base_address, prefix,
                         XX_SHRS_PREFIX_SIZE) ||
        xx_data_get_u32(prefix, sizeof(prefix), 0U, true) != XX_SHRS_MAGIC) {
        goto fail;
    }
    parsed->field_4 = xx_data_get_u32(prefix, sizeof(prefix), 4U, true);
    parsed->encrypted_data_size =
        xx_data_get_u32(prefix, sizeof(prefix), 8U, true);
    xx_rt_memcpy(parsed->iv, prefix + XX_SHRS_IV_OFFSET, XX_SHRS_IV_SIZE);

    if (parsed->encrypted_data_size == 0U) goto fail;

    /* The whole 0x6DC header has to be present before the payload can start,
     * and the payload itself has to be physically there.  binwalk skips this
     * second check and happily reports a multi-megabyte image inside a 40-byte
     * file; the declared length is straight out of the file and is the only
     * expansion lever this format has, so it is bounded here at parse. */
    if (!xx_shrs_add(self->base_address, XX_SHRS_HEADER_SIZE,
                     &parsed->encrypted_data_offset) ||
        !xx_shrs_range_within(parsed->input_size, parsed->encrypted_data_offset,
                              (int64_t)parsed->encrypted_data_size)) {
        goto fail;
    }
    if (!xx_shrs_add(self->base_address,
                     (uint64_t)XX_SHRS_HEADER_SIZE +
                         (uint64_t)parsed->encrypted_data_size,
                     &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        goto fail;
    }

    /* The IV lives inside the header, so it is published as a region pointing
     * back into it rather than being carved out of a payload area. */
    xx_shrs_push(parsed, "iv.bin",
                 self->base_address + (int64_t)XX_SHRS_IV_OFFSET,
                 (int64_t)XX_SHRS_IV_SIZE, false);
    xx_shrs_push(parsed, "encrypted.bin", parsed->encrypted_data_offset,
                 (int64_t)parsed->encrypted_data_size, true);
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_shrs_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_shrs_copy_options(xx_list_s *destination,
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

static const xx_var *xx_shrs_find_option(const xx_list_s *options,
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

static bool xx_shrs_populate_record(xx_archive_record *record,
                                    const xx_shrs_private *parsed,
                                    const xx_shrs_region *region) {
    if (!record || !parsed || !region || !region->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        parsed->encrypted_data_offset - (int64_t)XX_SHRS_HEADER_SIZE;
    record->header_size = (int64_t)XX_SHRS_HEADER_SIZE;
    record->data_offset = region->data_offset;
    record->compressed_size = region->data_size;
    /* Nothing inside an SHRS image is compressed, so the two sizes agree and
     * the compression method is "none". */
    return xx_archive_record_set_original_name(record, region->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           region->is_encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_shrs_archive_stream_free(void *pointer) {
    xx_shrs_archive_stream *stream = (xx_shrs_archive_stream *)pointer;
    if (!stream) return;
    xx_shrs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_shrs_init(xx_shrs *shrs, xx_io_device *dev, int64_t base_address) {
    if (!shrs) return;
    xx_mem_zero(shrs, sizeof(*shrs));
    xx_format_init(&shrs->format, dev, base_address);
    shrs->format.endian = XX_ENDIAN_BIG;
    shrs->format.file_type = XX_SHRS_FILE_TYPE;
    shrs->format.format_type = XX_TYPE_ARCHIVE;
    shrs->format.is_archive = true;
    shrs->format.is_crypted = true;
    xx_format_set_mime_type(&shrs->format, "application/x-dlink-shrs");
    xx_format_set_extension(&shrs->format, "bin");
    shrs->format.check_is_valid = xx_shrs_check_is_valid;
    shrs->format.handle_base_info = xx_shrs_handle_base_info;
    shrs->format.get_format_size = xx_shrs_get_format_size;
    shrs->format.get_number_of_archive_records =
        xx_shrs_get_number_of_archive_records;
    shrs->format.create_archive_records_reading =
        xx_shrs_create_archive_records_reading;
    shrs->format.get_current_archive_record =
        xx_shrs_get_current_archive_record;
    shrs->format.unpack_current_archive_record =
        xx_shrs_unpack_current_archive_record;
    shrs->format.archive_record_move_to_next =
        xx_shrs_archive_record_move_to_next;
    shrs->format.free_archive_records_reading =
        xx_shrs_free_archive_records_reading;
    shrs->format.destroy = xx_shrs_vtable_destroy;
    shrs->header_size = XX_SHRS_HEADER_SIZE;
    shrs->encrypted_data_offset = -1;
    shrs->archive_end = -1;
}

xx_shrs *xx_shrs_create(xx_io_device *dev, int64_t base_address) {
    xx_shrs *shrs = (xx_shrs *)xx_mem_alloc(sizeof(*shrs));
    if (shrs) xx_shrs_init(shrs, dev, base_address);
    return shrs;
}

void xx_shrs_destroy(xx_shrs *shrs) {
    if (!shrs) return;
    if (shrs->internal) {
        xx_shrs_private_cleanup((xx_shrs_private *)shrs->internal);
        xx_mem_free(shrs->internal);
        shrs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&shrs->format);
}

static void xx_shrs_vtable_destroy(Abstractformat *self) {
    xx_shrs_destroy((xx_shrs *)self);
}

void xx_shrs_free(xx_shrs *shrs) {
    if (!shrs) return;
    xx_shrs_destroy(shrs);
    xx_mem_free(shrs);
}

bool xx_shrs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_shrs_private parsed;
    bool result = xx_shrs_parse(self, &parsed, pd);
    xx_shrs_private_cleanup(&parsed);
    return result;
}

bool xx_shrs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_shrs_private *parsed;
    xx_shrs *shrs = (xx_shrs *)self;
    int64_t total_size;
    if (!self || !shrs) return false;
    parsed = (xx_shrs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_shrs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (shrs->internal) {
        xx_shrs_private_cleanup((xx_shrs_private *)shrs->internal);
        xx_mem_free(shrs->internal);
    }
    shrs->internal = parsed;
    shrs->number_of_records = parsed->count;
    shrs->header_size = XX_SHRS_HEADER_SIZE;
    shrs->encrypted_data_size = parsed->encrypted_data_size;
    shrs->field_4 = parsed->field_4;
    xx_rt_memcpy(shrs->iv, parsed->iv, XX_SHRS_IV_SIZE);
    shrs->encrypted_data_offset = parsed->encrypted_data_offset;
    shrs->archive_end = parsed->archive_end;
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

int64_t xx_shrs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_shrs_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_shrs *)self)->number_of_records;
}

xx_archive_record_state *xx_shrs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_shrs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_shrs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_shrs_copy_options(&state->options, options) ||
        !xx_shrs_parse(self, &stream->parsed, pd)) {
        xx_shrs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_shrs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_shrs_populate_record(&state->current_record, &stream->parsed,
                                &stream->parsed.regions[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_shrs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_shrs_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_shrs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_shrs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_shrs_populate_record(&state->current_record, &stream->parsed,
                                 &stream->parsed.regions[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_shrs_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_shrs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the region's span is addressable. */
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

void xx_shrs_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_shrs_get_number_of_records(const xx_shrs *shrs) {
    return shrs ? shrs->number_of_records : 0U;
}
uint32_t xx_shrs_get_encrypted_data_size(const xx_shrs *shrs) {
    return shrs ? shrs->encrypted_data_size : 0U;
}
const uint8_t *xx_shrs_get_iv(const xx_shrs *shrs) {
    return shrs ? shrs->iv : NULL;
}
int64_t xx_shrs_get_archive_end(const xx_shrs *shrs) {
    return shrs ? shrs->archive_end : -1;
}
