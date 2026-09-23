/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dmsfw/xx_dmsfw.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DMSFW exists in the enum. */
#ifdef DMSFW
#define XX_DMSFW_FILE_TYPE XX_FILE_TYPE_DMSFW
#else
#define XX_DMSFW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The single record: binwalk's swapped.rs writes the same name. */
#define XX_DMSFW_MEMBER_NAME "swapped.bin"
/** Staging buffer for un-swapping; a multiple of XX_DMSFW_SWAP_UNIT. */
#define XX_DMSFW_CHUNK_SIZE 0x10000U

typedef struct xx_dmsfw_private_s {
    int64_t input_size;
    int64_t header_offset;
    int64_t image_end;
    uint32_t image_size;
    uint32_t unswapped_size;
    uint32_t unknown2;
    uint16_t unknown1;
    size_t count;
} xx_dmsfw_private;

typedef struct xx_dmsfw_archive_stream_s {
    xx_dmsfw_private parsed;
    size_t index;
} xx_dmsfw_archive_stream;

static void xx_dmsfw_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64 and an image
 * of this kind is usually found inside a larger flash dump. */
static bool xx_dmsfw_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_dmsfw_write_all(xx_io_device *device, const uint8_t *data,
                               size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t put = xx_io_write(device, data + done, size - done);
        if (put <= 0 || (size_t)put > size - done) return false;
        done += (size_t)put;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dmsfw_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* binwalk's byte_swap(data, 2): in every complete 4-byte group ABCD the two
 * halves change places, giving CDAB.  The operation is its own inverse.  The
 * caller only passes whole groups; a trailing partial group is left alone
 * here and never published (see the header comment). */
static void xx_dmsfw_swap_halves(uint8_t *data, size_t size) {
    size_t index;
    if (!data) return;
    for (index = 0U; index + XX_DMSFW_SWAP_UNIT <= size;
         index += XX_DMSFW_SWAP_UNIT) {
        uint8_t first = data[index];
        uint8_t second = data[index + 1U];
        data[index] = data[index + 2U];
        data[index + 1U] = data[index + 3U];
        data[index + 2U] = first;
        data[index + 3U] = second;
    }
}

static void xx_dmsfw_private_cleanup(xx_dmsfw_private *parsed) {
    if (!parsed) return;
    /* Nothing here owns heap memory; the cleanup exists for symmetry with the
     * other readers and to leave a failed parse in a defined state. */
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->header_offset = -1;
    parsed->image_end = -1;
}

/*
 * binwalk accepts the image when 0x100 bytes are present, the un-swapped
 * header carries 0x4D47 at +2 and 0x3C31303E at +4, and the size word fits
 * the file (its scanner drops a result that runs past EOF).  Two cases it
 * lets through are refused here because neither describes an image:
 *   - a size of zero, which binwalk treats as "unknown" and replaces with the
 *     distance to the next signature or EOF, and
 *   - a size below 0x100, i.e. an image shorter than the window binwalk
 *     itself had to read to validate it (it could not even hold that
 *     window, and would end inside a sixteen-byte header for sizes < 16).
 * There is no checksum, so the two magic words plus the size-vs-device check
 * are all the validation the format offers.
 */
static bool xx_dmsfw_parse(Abstractformat *self, xx_dmsfw_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t raw[XX_DMSFW_HEADER_SIZE];
    uint8_t header[XX_DMSFW_HEADER_SIZE];
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->header_offset = -1;
        parsed->image_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->header_offset = self->base_address;
    if (!xx_dmsfw_range_within(parsed->input_size, self->base_address,
                               XX_DMSFW_MIN_SIZE) ||
        !xx_dmsfw_read_at(self->device, self->base_address, raw,
                          sizeof(raw)) ||
        xx_rt_memcmp(raw, XX_DMSFW_RAW_TAG, XX_DMSFW_RAW_TAG_SIZE) != 0 ||
        xx_rt_memcmp(raw + XX_DMSFW_RAW_MAGIC_OFFSET, XX_DMSFW_RAW_MAGIC,
                     XX_DMSFW_RAW_MAGIC_SIZE) != 0) {
        goto fail;
    }
    xx_rt_memcpy(header, raw, sizeof(header));
    xx_dmsfw_swap_halves(header, sizeof(header));
    /* Redundant with the raw compares above, but this is the check binwalk's
     * structures/dms.rs performs, kept verbatim on the un-swapped words. */
    if (xx_data_get_u16(header, sizeof(header), 0x02U, true) !=
            XX_DMSFW_MAGIC_P1 ||
        xx_data_get_u32(header, sizeof(header), 0x04U, true) !=
            (uint32_t)XX_DMSFW_MAGIC_P2) {
        goto fail;
    }
    parsed->unknown1 = xx_data_get_u16(header, sizeof(header), 0x00U, true);
    parsed->unknown2 = xx_data_get_u32(header, sizeof(header), 0x08U, true);
    parsed->image_size = xx_data_get_u32(header, sizeof(header), 0x0CU, true);
    if (parsed->image_size < XX_DMSFW_MIN_SIZE ||
        !xx_dmsfw_range_within(parsed->input_size, self->base_address,
                               (int64_t)parsed->image_size)) {
        goto fail;
    }
    /* range_within guarantees base + size <= input_size, so no overflow. */
    parsed->image_end = self->base_address + (int64_t)parsed->image_size;
    parsed->unswapped_size =
        parsed->image_size & ~(uint32_t)(XX_DMSFW_SWAP_UNIT - 1U);
    parsed->count = 1U;
    return true;
fail:
    xx_dmsfw_private_cleanup(parsed);
    return false;
}

/* Streams the un-swapped image to @p output in bounded chunks.  The output
 * is never larger than the carved input, so there is nothing to cap beyond
 * the fixed staging buffer. */
static bool xx_dmsfw_stream(xx_io_device *device, const xx_dmsfw_private *parsed,
                            xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *buffer;
    uint32_t remaining;
    int64_t position;
    bool ok = true;
    if (!device || !parsed || !output || parsed->count == 0U ||
        parsed->header_offset < 0 ||
        !xx_dmsfw_range_within(xx_io_total_size(device), parsed->header_offset,
                               (int64_t)parsed->unswapped_size)) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(XX_DMSFW_CHUNK_SIZE);
    if (!buffer) return false;
    remaining = parsed->unswapped_size;
    position = parsed->header_offset;
    /* Each pass consumes at least one 4-byte group (remaining is a multiple
     * of four and non-zero inside the loop), so the loop is bounded by
     * 2^32 / 4 passes and in practice by size / 64 KiB. */
    while (remaining != 0U) {
        size_t step = remaining < XX_DMSFW_CHUNK_SIZE ? (size_t)remaining
                                                      : XX_DMSFW_CHUNK_SIZE;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_dmsfw_read_at(device, position, buffer, step)) {
            ok = false;
            break;
        }
        xx_dmsfw_swap_halves(buffer, step);
        if (!xx_dmsfw_write_all(output, buffer, step)) {
            ok = false;
            break;
        }
        position += (int64_t)step;
        remaining -= (uint32_t)step;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dmsfw_copy_options(xx_list_s *destination,
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

static const xx_var *xx_dmsfw_find_option(const xx_list_s *options,
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

static bool xx_dmsfw_populate_record(xx_archive_record *record,
                                     const xx_dmsfw_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->header_offset;
    record->header_size = XX_DMSFW_HEADER_SIZE;
    /* The header is part of the image and is published un-swapped with it. */
    record->data_offset = parsed->header_offset;
    record->compressed_size = (int64_t)parsed->image_size;
    /* The name is a literal chosen here, never taken from the file, so it
     * needs no sanitising before use as a destination path component. */
    return xx_archive_record_set_original_name(record, XX_DMSFW_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          parsed->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          parsed->unswapped_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_dmsfw_archive_stream_free(void *pointer) {
    xx_dmsfw_archive_stream *stream = (xx_dmsfw_archive_stream *)pointer;
    if (!stream) return;
    xx_dmsfw_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dmsfw_init(xx_dmsfw *dmsfw, xx_io_device *dev, int64_t base_address) {
    if (!dmsfw) return;
    xx_mem_zero(dmsfw, sizeof(*dmsfw));
    xx_format_init(&dmsfw->format, dev, base_address);
    dmsfw->format.endian = XX_ENDIAN_BIG;
    dmsfw->format.file_type = XX_DMSFW_FILE_TYPE;
    dmsfw->format.format_type = XX_TYPE_ARCHIVE;
    dmsfw->format.is_archive = true;
    xx_format_set_mime_type(&dmsfw->format, "application/x-dms-firmware");
    xx_format_set_extension(&dmsfw->format, "bin");
    dmsfw->format.check_is_valid = xx_dmsfw_check_is_valid;
    dmsfw->format.handle_base_info = xx_dmsfw_handle_base_info;
    dmsfw->format.get_format_size = xx_dmsfw_get_format_size;
    dmsfw->format.get_number_of_archive_records =
        xx_dmsfw_get_number_of_archive_records;
    dmsfw->format.create_archive_records_reading =
        xx_dmsfw_create_archive_records_reading;
    dmsfw->format.get_current_archive_record =
        xx_dmsfw_get_current_archive_record;
    dmsfw->format.unpack_current_archive_record =
        xx_dmsfw_unpack_current_archive_record;
    dmsfw->format.archive_record_move_to_next =
        xx_dmsfw_archive_record_move_to_next;
    dmsfw->format.free_archive_records_reading =
        xx_dmsfw_free_archive_records_reading;
    dmsfw->format.destroy = xx_dmsfw_vtable_destroy;
    dmsfw->image_end = -1;
}

xx_dmsfw *xx_dmsfw_create(xx_io_device *dev, int64_t base_address) {
    xx_dmsfw *dmsfw = (xx_dmsfw *)xx_mem_alloc(sizeof(*dmsfw));
    if (dmsfw) xx_dmsfw_init(dmsfw, dev, base_address);
    return dmsfw;
}

void xx_dmsfw_destroy(xx_dmsfw *dmsfw) {
    if (!dmsfw) return;
    if (dmsfw->internal) {
        xx_dmsfw_private_cleanup((xx_dmsfw_private *)dmsfw->internal);
        xx_mem_free(dmsfw->internal);
        dmsfw->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dmsfw->format);
}

static void xx_dmsfw_vtable_destroy(Abstractformat *self) {
    xx_dmsfw_destroy((xx_dmsfw *)self);
}

void xx_dmsfw_free(xx_dmsfw *dmsfw) {
    if (!dmsfw) return;
    xx_dmsfw_destroy(dmsfw);
    xx_mem_free(dmsfw);
}

bool xx_dmsfw_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dmsfw_private parsed;
    bool result = xx_dmsfw_parse(self, &parsed, pd);
    xx_dmsfw_private_cleanup(&parsed);
    return result;
}

bool xx_dmsfw_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dmsfw_private *parsed;
    xx_dmsfw *dmsfw = (xx_dmsfw *)self;
    int64_t total_size;
    if (!self || !dmsfw) return false;
    parsed = (xx_dmsfw_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dmsfw_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dmsfw->internal) {
        xx_dmsfw_private_cleanup((xx_dmsfw_private *)dmsfw->internal);
        xx_mem_free(dmsfw->internal);
    }
    dmsfw->internal = parsed;
    dmsfw->number_of_records = parsed->count;
    dmsfw->number_of_members = parsed->count;
    dmsfw->image_size = parsed->image_size;
    dmsfw->unswapped_size = parsed->unswapped_size;
    dmsfw->unknown1 = parsed->unknown1;
    dmsfw->unknown2 = parsed->unknown2;
    dmsfw->image_end = parsed->image_end;
    self->format_size = parsed->image_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->image_end) {
        self->overlay_offset = parsed->image_end;
        self->overlay_size = total_size - parsed->image_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dmsfw_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dmsfw_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dmsfw *)self)->number_of_records;
}

xx_archive_record_state *xx_dmsfw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dmsfw_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dmsfw_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dmsfw_copy_options(&state->options, options) ||
        !xx_dmsfw_parse(self, &stream->parsed, pd)) {
        xx_dmsfw_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dmsfw_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_dmsfw_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dmsfw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dmsfw_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_dmsfw_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dmsfw_archive_stream *)state->internal_state;
    ++stream->index;
    /* One image only, so the first move always ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_dmsfw_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_dmsfw_archive_stream *stream;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (const xx_dmsfw_archive_stream *)state->internal_state;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_dmsfw_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return stream->parsed.count != 0U && record->data_offset >= 0 &&
               record->compressed_size >= 0 && record->data_offset <= total &&
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
    output = xx_io_file_open(destination, "wb");
    if (!output) goto cleanup;
    result = xx_dmsfw_stream(self->device, &stream->parsed, output, pd);
    if (xx_io_close(output) != 0) result = false;
    output = NULL;
    if (!result) xx_rt_remove(destination);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_dmsfw_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

bool xx_dmsfw_unpack_to_device(xx_dmsfw *dmsfw, xx_io_device *output,
                               xx_pd_struct *pd) {
    xx_dmsfw_private parsed;
    bool result;
    if (!dmsfw || !output) return false;
    result = xx_dmsfw_parse(&dmsfw->format, &parsed, pd) &&
             xx_dmsfw_stream(dmsfw->format.device, &parsed, output, pd);
    xx_dmsfw_private_cleanup(&parsed);
    return result;
}

uint64_t xx_dmsfw_get_number_of_records(const xx_dmsfw *dmsfw) {
    return dmsfw ? dmsfw->number_of_records : 0U;
}
uint64_t xx_dmsfw_get_number_of_members(const xx_dmsfw *dmsfw) {
    return dmsfw ? dmsfw->number_of_members : 0U;
}
uint32_t xx_dmsfw_get_image_size(const xx_dmsfw *dmsfw) {
    return dmsfw ? dmsfw->image_size : 0U;
}
uint32_t xx_dmsfw_get_unswapped_size(const xx_dmsfw *dmsfw) {
    return dmsfw ? dmsfw->unswapped_size : 0U;
}
int64_t xx_dmsfw_get_image_end(const xx_dmsfw *dmsfw) {
    return dmsfw ? dmsfw->image_end : -1;
}
