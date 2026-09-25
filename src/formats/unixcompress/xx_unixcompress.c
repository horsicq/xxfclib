/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/unixcompress/xx_unixcompress.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_UNIXCOMPRESS_PAYLOAD_NAME "payload"
#define XX_UNIXCOMPRESS_MAX_INPUT ((uint64_t)1024U * 1024U * 1024U)
#define XX_UNIXCOMPRESS_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)

#define XX_UNIXCOMPRESS_HEADER_SIZE 3U
#define XX_UNIXCOMPRESS_MAGIC0 UINT8_C(0x1f)
#define XX_UNIXCOMPRESS_MAGIC1 UINT8_C(0x9d)
#define XX_UNIXCOMPRESS_FLAG_BLOCK UINT8_C(0x80)
#define XX_UNIXCOMPRESS_FLAG_RESERVED UINT8_C(0x60)
#define XX_UNIXCOMPRESS_FLAG_BITS UINT8_C(0x1f)
#define XX_UNIXCOMPRESS_MIN_BITS 9U
#define XX_UNIXCOMPRESS_MAX_BITS 16U
#define XX_UNIXCOMPRESS_CLEAR 256U
#define XX_UNIXCOMPRESS_TABLE_SIZE (UINT32_C(1) << XX_UNIXCOMPRESS_MAX_BITS)
#define XX_UNIXCOMPRESS_IN_CHUNK 16384U
#define XX_UNIXCOMPRESS_OUT_CHUNK 16384U
#define XX_UNIXCOMPRESS_STOP_CHECK 0x0fffU

/* How a stream whose header says maxbits == 9 is read once its table is
 * full.  The original compress 4.0 code (and its descendants: BSD compress,
 * ncompress, gzip's unlzw) still grows the code width to 10 bits at that
 * point; 7-Zip, unar and deark keep 9-bit codes.  Both forms exist, so the
 * reader tries the compress 4.0 form first (a 7-Zip-form stream read that way
 * fails within a few codes because the 10-bit values exceed the table) and
 * falls back to 9-bit codes.  For every other maxbits the two are identical. */
typedef enum xx_unixcompress_variant_e {
    XX_UNIXCOMPRESS_VARIANT_AUTO = 0,
    XX_UNIXCOMPRESS_VARIANT_WIDEN9,
    XX_UNIXCOMPRESS_VARIANT_STANDARD
} xx_unixcompress_variant;

/* Abstractformat::priv marks a maxbits-9 stream that only decodes in the
 * 9-bit (7-Zip) form; it points at this tag and owns nothing. */
static const uint8_t k_xx_unixcompress_standard_tag = 0U;

typedef enum xx_unixcompress_status_e {
    XX_UNIXCOMPRESS_OK = 0,
    XX_UNIXCOMPRESS_BAD_DATA,
    XX_UNIXCOMPRESS_FAILED
} xx_unixcompress_status;

typedef struct xx_unixcompress_lzw_s {
    xx_io_device *source;
    int64_t source_remaining;
    uint64_t body_bits;
    uint64_t bit_index;
    uint64_t group_start;
    size_t in_used;
    size_t in_position;
    uint32_t bit_buffer;
    unsigned bit_count;

    xx_io_device *target;
    uint64_t output_total;
    uint64_t output_limit;
    size_t out_used;
    xx_pd_struct *pd;

    uint16_t prefix[XX_UNIXCOMPRESS_TABLE_SIZE];
    uint8_t suffix[XX_UNIXCOMPRESS_TABLE_SIZE];
    uint8_t stack[XX_UNIXCOMPRESS_TABLE_SIZE + 1U];
    uint8_t in[XX_UNIXCOMPRESS_IN_CHUNK];
    uint8_t out[XX_UNIXCOMPRESS_OUT_CHUNK];
} xx_unixcompress_lzw;

static void xx_unixcompress_vtable_destroy(Abstractformat *self);

static bool xx_unixcompress_read_exact(xx_io_device *device, void *buffer,
                                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U)) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_unixcompress_header_ok(const uint8_t *header,
                                      unsigned *maximum_bits,
                                      bool *block_mode) {
    unsigned bits;
    if (!header || header[0] != XX_UNIXCOMPRESS_MAGIC0 ||
        header[1] != XX_UNIXCOMPRESS_MAGIC1 ||
        (header[2] & XX_UNIXCOMPRESS_FLAG_RESERVED) != 0U) {
        return false;
    }
    bits = header[2] & XX_UNIXCOMPRESS_FLAG_BITS;
    if (bits < XX_UNIXCOMPRESS_MIN_BITS || bits > XX_UNIXCOMPRESS_MAX_BITS) {
        return false;
    }
    if (maximum_bits) *maximum_bits = bits;
    if (block_mode) {
        *block_mode = (header[2] & XX_UNIXCOMPRESS_FLAG_BLOCK) != 0U;
    }
    return true;
}

/* 1 = code, 0 = fewer than @p width bits left (clean end), -1 = I/O error.
 * Bits after the last whole code are ignored, as compress and 7-Zip do. */
static int xx_unixcompress_get_bits(xx_unixcompress_lzw *z, unsigned width,
                                    uint32_t *value) {
    if (width == 0U || width > XX_UNIXCOMPRESS_MAX_BITS ||
        z->bit_index > z->body_bits) {
        return -1;
    }
    if (z->body_bits - z->bit_index < width) return 0;
    while (z->bit_count < width) {
        if (z->in_position == z->in_used) {
            size_t wanted;
            if (z->source_remaining <= 0) return -1;
            wanted = z->source_remaining > (int64_t)XX_UNIXCOMPRESS_IN_CHUNK
                         ? XX_UNIXCOMPRESS_IN_CHUNK
                         : (size_t)z->source_remaining;
            if (!xx_unixcompress_read_exact(z->source, z->in, wanted)) {
                return -1;
            }
            z->source_remaining -= (int64_t)wanted;
            z->in_used = wanted;
            z->in_position = 0U;
        }
        z->bit_buffer |= (uint32_t)z->in[z->in_position++] << z->bit_count;
        z->bit_count += 8U;
    }
    *value = z->bit_buffer & ((UINT32_C(1) << width) - UINT32_C(1));
    z->bit_buffer >>= width;
    z->bit_count -= width;
    z->bit_index += width;
    return 1;
}

/* Codes travel in groups of eight; a width change or CLEAR discards the rest
 * of the current group.  1 = aligned, 0 = the padding runs past the end of
 * the stream (clean end), -1 = I/O error. */
static int xx_unixcompress_align(xx_unixcompress_lzw *z, unsigned width) {
    uint64_t group = (uint64_t)width * 8U;
    uint64_t used;
    uint64_t padding;
    if (z->bit_index < z->group_start || z->bit_index > z->body_bits) {
        return -1;
    }
    used = z->bit_index - z->group_start;
    padding = (group - used % group) % group;
    if (padding > z->body_bits - z->bit_index) {
        z->bit_index = z->body_bits;
        return 0;
    }
    while (padding != 0U) {
        unsigned take = padding > XX_UNIXCOMPRESS_MAX_BITS
                            ? XX_UNIXCOMPRESS_MAX_BITS
                            : (unsigned)padding;
        uint32_t ignored;
        if (xx_unixcompress_get_bits(z, take, &ignored) != 1) return -1;
        padding -= take;
    }
    z->group_start = z->bit_index;
    return 1;
}

static bool xx_unixcompress_flush(xx_unixcompress_lzw *z) {
    size_t done = 0U;
    if (z->pd && xx_pd_is_stopped(z->pd)) return false;
    while (z->target && done < z->out_used) {
        ssize_t amount = xx_io_write(z->target, z->out + done,
                                     z->out_used - done);
        if (amount <= 0 || (size_t)amount > z->out_used - done) return false;
        done += (size_t)amount;
    }
    z->out_used = 0U;
    return true;
}

/* Emit stack[0..count) in reverse order. */
static bool xx_unixcompress_emit_stack(xx_unixcompress_lzw *z, size_t count) {
    if ((uint64_t)count > z->output_limit - z->output_total) return false;
    z->output_total += (uint64_t)count;
    while (count != 0U) {
        size_t room = XX_UNIXCOMPRESS_OUT_CHUNK - z->out_used;
        size_t take = count < room ? count : room;
        size_t index;
        for (index = 0U; index < take; ++index) {
            z->out[z->out_used++] = z->stack[--count];
        }
        if (z->out_used == XX_UNIXCOMPRESS_OUT_CHUNK &&
            !xx_unixcompress_flush(z)) {
            return false;
        }
    }
    return true;
}

static xx_unixcompress_status xx_unixcompress_lzw_run(
    xx_unixcompress_lzw *z, unsigned maximum_bits, bool block_mode,
    bool widen9, bool *widened) {
    uint32_t maximum_codes = UINT32_C(1) << maximum_bits;
    uint32_t first_free = block_mode ? XX_UNIXCOMPRESS_CLEAR + 1U
                                     : XX_UNIXCOMPRESS_CLEAR;
    uint32_t next_code = first_free;
    uint32_t old_code = 0U;
    uint32_t code;
    uint32_t counter = 0U;
    uint8_t final_byte = 0U;
    unsigned width = XX_UNIXCOMPRESS_MIN_BITS;
    bool have_old = false;

    for (;;) {
        uint32_t input_code;
        size_t count = 0U;
        int status = xx_unixcompress_get_bits(z, width, &code);
        if (status == 0) break;
        if (status < 0) return XX_UNIXCOMPRESS_FAILED;
        if ((++counter & XX_UNIXCOMPRESS_STOP_CHECK) == 0U && z->pd &&
            xx_pd_is_stopped(z->pd)) {
            return XX_UNIXCOMPRESS_FAILED;
        }
        if (block_mode && code == XX_UNIXCOMPRESS_CLEAR) {
            /* Accepted anywhere, repeated or last, like 7-Zip. */
            status = xx_unixcompress_align(z, width);
            if (status == 0) break;
            if (status < 0) return XX_UNIXCOMPRESS_FAILED;
            width = XX_UNIXCOMPRESS_MIN_BITS;
            next_code = first_free;
            have_old = false;
            continue;
        }
        if (!have_old) {
            if (code >= XX_UNIXCOMPRESS_CLEAR) return XX_UNIXCOMPRESS_BAD_DATA;
            z->stack[0] = (uint8_t)code;
            if (!xx_unixcompress_emit_stack(z, 1U)) {
                return XX_UNIXCOMPRESS_FAILED;
            }
            old_code = code;
            final_byte = (uint8_t)code;
            have_old = true;
            continue;
        }

        input_code = code;
        if (code >= next_code) {
            /* Only the slot being defined right now may be referenced. */
            if (code != next_code || next_code >= maximum_codes) {
                return XX_UNIXCOMPRESS_BAD_DATA;
            }
            z->stack[count++] = final_byte;
            code = old_code;
        }
        /* prefix[c] < c for every defined slot, so the walk terminates;
         * the bound is a second guard. */
        while (code >= XX_UNIXCOMPRESS_CLEAR) {
            if (code >= next_code || count >= XX_UNIXCOMPRESS_TABLE_SIZE) {
                return XX_UNIXCOMPRESS_BAD_DATA;
            }
            z->stack[count++] = z->suffix[code];
            code = z->prefix[code];
        }
        final_byte = (uint8_t)code;
        z->stack[count++] = final_byte;
        if (!xx_unixcompress_emit_stack(z, count)) {
            return XX_UNIXCOMPRESS_FAILED;
        }

        if (next_code < maximum_codes) {
            z->prefix[next_code] = (uint16_t)old_code;
            z->suffix[next_code] = final_byte;
            ++next_code;
            if (next_code >= (UINT32_C(1) << width) &&
                (width < maximum_bits ||
                 (widen9 && maximum_bits == XX_UNIXCOMPRESS_MIN_BITS &&
                  width == XX_UNIXCOMPRESS_MIN_BITS))) {
                if (width >= maximum_bits && widened) *widened = true;
                status = xx_unixcompress_align(z, width);
                if (status == 0) break;
                if (status < 0) return XX_UNIXCOMPRESS_FAILED;
                ++width;
            }
        }
        old_code = input_code;
    }
    return xx_unixcompress_flush(z) ? XX_UNIXCOMPRESS_OK
                                    : XX_UNIXCOMPRESS_FAILED;
}

/* Decode the whole range [base, end of device) with one variant. */
static xx_unixcompress_status xx_unixcompress_decode_once(
    xx_unixcompress_lzw *z, Abstractformat *self, int64_t input_size,
    xx_io_device *destination, unsigned maximum_bits, bool block_mode,
    bool widen9, bool *widened, xx_pd_struct *pd) {
    uint8_t header[XX_UNIXCOMPRESS_HEADER_SIZE];
    z->source = self->device;
    z->source_remaining = input_size - (int64_t)XX_UNIXCOMPRESS_HEADER_SIZE;
    z->body_bits = (uint64_t)z->source_remaining * 8U;
    z->bit_index = 0U;
    z->group_start = 0U;
    z->in_used = 0U;
    z->in_position = 0U;
    z->bit_buffer = 0U;
    z->bit_count = 0U;
    z->target = destination;
    z->output_total = 0U;
    z->output_limit = XX_UNIXCOMPRESS_MAX_OUTPUT;
    z->out_used = 0U;
    z->pd = pd;
    if (widened) *widened = false;
    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0 ||
        !xx_unixcompress_read_exact(self->device, header, sizeof(header))) {
        return XX_UNIXCOMPRESS_FAILED;
    }
    return xx_unixcompress_lzw_run(z, maximum_bits, block_mode, widen9,
                                   widened);
}

/* Unix compress carries no end marker or checksum.  A standalone member
 * therefore occupies the entire supplied device range. */
static bool xx_unixcompress_decode_stream(Abstractformat *self,
                                          xx_io_device *destination,
                                          xx_unixcompress_variant variant,
                                          xx_unixcompress_variant *used,
                                          uint64_t *uncompressed_size,
                                          int64_t *stream_size,
                                          xx_pd_struct *pd) {
    uint8_t header[XX_UNIXCOMPRESS_HEADER_SIZE];
    int64_t total_size;
    int64_t input_size;
    unsigned maximum_bits = 0U;
    bool block_mode = false;
    bool widened = false;
    xx_unixcompress_lzw *z;
    xx_unixcompress_status status;
    xx_unixcompress_variant chosen;
    if (!self || !self->device || self->base_address < 0 ||
        !uncompressed_size || !stream_size ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address <
            (int64_t)XX_UNIXCOMPRESS_HEADER_SIZE ||
        (uint64_t)(total_size - self->base_address) >
            XX_UNIXCOMPRESS_MAX_INPUT) {
        return false;
    }
    input_size = total_size - self->base_address;
    /* Cheap header check before the table allocation. */
    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0 ||
        !xx_unixcompress_read_exact(self->device, header, sizeof(header)) ||
        !xx_unixcompress_header_ok(header, &maximum_bits, &block_mode)) {
        return false;
    }
    /* A destination can only be written once, so it needs a known form. */
    if (destination && variant == XX_UNIXCOMPRESS_VARIANT_AUTO) return false;
    chosen = variant;
    if (chosen == XX_UNIXCOMPRESS_VARIANT_AUTO) {
        chosen = maximum_bits == XX_UNIXCOMPRESS_MIN_BITS
                     ? XX_UNIXCOMPRESS_VARIANT_WIDEN9
                     : XX_UNIXCOMPRESS_VARIANT_STANDARD;
    }
    z = (xx_unixcompress_lzw *)xx_mem_alloc(sizeof(*z));
    if (!z) return false;
    xx_mem_zero(z, sizeof(*z));
    status = xx_unixcompress_decode_once(
        z, self, input_size, destination, maximum_bits, block_mode,
        chosen == XX_UNIXCOMPRESS_VARIANT_WIDEN9, &widened, pd);
    if (variant == XX_UNIXCOMPRESS_VARIANT_AUTO &&
        status == XX_UNIXCOMPRESS_BAD_DATA &&
        chosen == XX_UNIXCOMPRESS_VARIANT_WIDEN9 && widened) {
        /* The 10-bit reading went wrong after the table filled: try the
         * 9-bit form.  Nothing was written (destination is NULL here). */
        chosen = XX_UNIXCOMPRESS_VARIANT_STANDARD;
        status = xx_unixcompress_decode_once(z, self, input_size, NULL,
                                             maximum_bits, block_mode, false,
                                             NULL, pd);
    }
    if (status == XX_UNIXCOMPRESS_OK) {
        *uncompressed_size = z->output_total;
        *stream_size = input_size;
        if (used) *used = chosen;
    }
    xx_mem_free(z);
    return status == XX_UNIXCOMPRESS_OK;
}

static xx_unixcompress_variant xx_unixcompress_known_variant(
    const Abstractformat *self) {
    return self && self->priv == (const void *)&k_xx_unixcompress_standard_tag
               ? XX_UNIXCOMPRESS_VARIANT_STANDARD
               : XX_UNIXCOMPRESS_VARIANT_WIDEN9;
}

static bool xx_unixcompress_copy_options(xx_list_s *destination,
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

static const xx_var *xx_unixcompress_find_option(const xx_list_s *options,
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

static bool xx_unixcompress_populate_record(Abstractformat *self,
                                            xx_archive_record *record) {
    const xx_unixcompress *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 3) {
        return false;
    }
    archive = (const xx_unixcompress *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = 3;
    record->data_offset = self->base_address + 3;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_UNIXCOMPRESS_PAYLOAD_NAME) &&
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

void xx_unixcompress_init(xx_unixcompress *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_UNIX_COMPRESS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compress");
    xx_format_set_extension(&archive->format, "Z");
    archive->format.check_is_valid = xx_unixcompress_check_is_valid;
    archive->format.handle_base_info = xx_unixcompress_handle_base_info;
    archive->format.get_format_size = xx_unixcompress_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_unixcompress_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_unixcompress_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_unixcompress_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_unixcompress_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_unixcompress_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_unixcompress_free_archive_records_reading;
    archive->format.destroy = xx_unixcompress_vtable_destroy;
    archive->stream_end = -1;
}

xx_unixcompress *xx_unixcompress_create(xx_io_device *device,
                                         int64_t base_address) {
    xx_unixcompress *archive =
        (xx_unixcompress *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_unixcompress_init(archive, device, base_address);
    return archive;
}

void xx_unixcompress_destroy(xx_unixcompress *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->format.priv = NULL;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_unixcompress_vtable_destroy(Abstractformat *self) {
    xx_unixcompress_destroy((xx_unixcompress *)self);
}

void xx_unixcompress_free(xx_unixcompress *archive) {
    if (!archive) return;
    xx_unixcompress_destroy(archive);
    xx_mem_free(archive);
}

bool xx_unixcompress_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    return xx_unixcompress_decode_stream(self, NULL,
                                         XX_UNIXCOMPRESS_VARIANT_AUTO, NULL,
                                         &size, &stream_size, pd);
}

bool xx_unixcompress_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    xx_unixcompress_variant used = XX_UNIXCOMPRESS_VARIANT_AUTO;
    xx_unixcompress *archive;
    if (!self || !xx_unixcompress_decode_stream(self, NULL,
                                                XX_UNIXCOMPRESS_VARIANT_AUTO,
                                                &used, &size, &stream_size,
                                                pd)) {
        if (self) {
            archive = (xx_unixcompress *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            self->priv = NULL;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_unixcompress *)self;
    archive->uncompressed_size = size;
    archive->stream_end = self->base_address + stream_size;
    self->priv = used == XX_UNIXCOMPRESS_VARIANT_STANDARD
                     ? (void *)&k_xx_unixcompress_standard_tag
                     : NULL;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FILE_TYPE_UNIX_COMPRESS;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_unixcompress_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_unixcompress_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_unixcompress_unpack_to_device(xx_unixcompress *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd) {
    uint64_t size;
    int64_t stream_size;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_unixcompress_decode_stream(
            &archive->format, destination,
            xx_unixcompress_known_variant(&archive->format), NULL, &size,
            &stream_size, pd)) {
        return false;
    }
    return stream_size == archive->format.format_size &&
           size == archive->uncompressed_size;
}

xx_archive_record_state *xx_unixcompress_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_unixcompress_copy_options(&state->options, options) ||
        !xx_unixcompress_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_unixcompress_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record ?
               &state->current_record : NULL;
}

bool xx_unixcompress_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_unixcompress_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_unixcompress *archive = (xx_unixcompress *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_unixcompress_find_option(&state->options,
                                              XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        uint64_t size;
        int64_t stream_size;
        return xx_unixcompress_decode_stream(
                   self, NULL, xx_unixcompress_known_variant(self), NULL,
                   &size, &stream_size, pd) &&
               stream_size == self->format_size &&
               size == archive->uncompressed_size;
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
                                          XX_UNIXCOMPRESS_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_UNIXCOMPRESS_PAYLOAD_NAME);
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
        result = output && xx_unixcompress_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_unixcompress_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_unixcompress_get_uncompressed_size(
    const xx_unixcompress *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_unixcompress_get_stream_end(const xx_unixcompress *archive) {
    return archive ? archive->stream_end : -1;
}
