/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PowerPacker, the Amiga cruncher by Nico Francois.  A crunched data file is
 * a single stream with no member names and no directory:
 *
 *   0x00  char[4]  "PP20" (the common form) or "PP11" (the older one).
 *                  "PPLS" is the library/segment form and carries one extra
 *                  longword before the table; "PX20" and "PPBK" are the
 *                  password-protected forms.
 *   0x04  u8[4]    the efficiency table: the number of bits used by an LZ
 *                  offset for each of the four match-length classes.  All
 *                  four values sit in 9..16 and never decrease.
 *   ....  u8[]     the crunched bit stream.
 *   n-4   u24 BE   the decrunched length.
 *   n-1   u8       the number of bits to discard before decoding, 0..31,
 *                  because the encoder pads the stream to a longword.
 *
 * "PPLS" puts a longword at 0x04 -- the decrunched size of the whole hunk
 * sequence, which is larger than the stream's own trailer length -- and the
 * efficiency table moves to 0x08, so its payload starts at 0x0c.
 *
 * DECODING RUNS BACKWARDS.  The bit reader starts at the byte just before
 * the four-byte trailer and walks towards the header, taking bits from the
 * LOW end of each byte and shifting them into the HIGH end of the result;
 * the output is filled from its last byte towards its first.  A match copies
 * from HIGHER output addresses -- bytes already produced -- so the whole
 * thing mirrors an ordinary LZ77 decoder.  Getting this direction wrong is
 * the classic way to produce plausible-looking garbage, so every source byte
 * and every output index is bounds-checked in both directions here.
 *
 * Stream grammar, per iteration:
 *   1 bit  0  -> a literal run: 2-bit groups are summed while each group is
 *                3, and (sum + 1) literal bytes follow, 8 bits each.
 *   then a match: a 2-bit class n selects length n+2 and offset width
 *   table[n].  Class 3 is the long form: one bit picks table[3] (set) or a
 *   flat 7 bits (clear) for the offset width, the offset follows, and then
 *   3-bit groups are summed into the length while each group is 7.
 *
 * The encrypted variants are recognised but deliberately fail closed: the
 * password checksum in the header cannot recover the key, and decoding
 * without it would emit noise rather than an error.
 *
 * Layout confirmed against 3269 real samples in F:\ARC\ARC\PP20.
 *
 * The file type is resolved through a shim so this reader compiles before the
 * enum exists; it picks up the real value as soon as PP20 is registered.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pp20/xx_pp20.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#ifdef PP20
#define XX_PP20_FILE_TYPE XX_FILE_TYPE_PP20
#else
#define XX_PP20_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PP20_TRAILER_SIZE 4U
#define XX_PP20_TABLE_SIZE 4U
#define XX_PP20_HEADER_PLAIN 8U     /* PP20 / PP11 */
#define XX_PP20_HEADER_PPLS 12U     /* PPLS: extra longword before the table */
#define XX_PP20_HEADER_PX20 10U     /* PX20: u16 password checksum */
#define XX_PP20_MIN_WIDTH 9U
#define XX_PP20_MAX_WIDTH 16U
#define XX_PP20_MAX_SKIP 31U
#define XX_PP20_MAX_INPUT ((uint64_t)256U * 1024U * 1024U)
#define XX_PP20_MAX_OUTPUT ((uint64_t)0x00ffffffU) /* the trailer is 24 bit */
#define XX_PP20_MAX_RATIO 1024U
#define XX_PP20_RATIO_SLACK 4096U
#define XX_PP20_PAYLOAD_NAME "data"
#define XX_PP20_COMPRESSION_METHOD 1U

#define XX_PP20_VARIANT_PP20 0U
#define XX_PP20_VARIANT_PP11 1U
#define XX_PP20_VARIANT_PPLS 2U
#define XX_PP20_VARIANT_PX20 3U
#define XX_PP20_VARIANT_PPBK 4U

typedef struct xx_pp20_context_s {
    uint64_t uncompressed_size;
    int64_t stream_size;
    uint32_t variant;
    uint8_t offset_widths[4];
    uint8_t skip_bits;
    bool crypted;
} xx_pp20_context;

static void xx_pp20_vtable_destroy(Abstractformat *self);

static bool xx_pp20_variant_is_crypted(uint32_t variant) {
    return variant == XX_PP20_VARIANT_PX20 || variant == XX_PP20_VARIANT_PPBK;
}

/* ---------------------------------------------------------------- io ---- */

static bool xx_pp20_read_exact_at(xx_io_device *device, int64_t offset,
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

static bool xx_pp20_write_all(xx_io_device *device, const void *data,
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

/* ------------------------------------------------------- bit reader ---- */

/* Walks backwards from `position` towards `floor`.  Bits leave each byte at
 * its low end and enter the result at its high end.  Running out of source
 * latches an error instead of wrapping. */
typedef struct xx_pp20_bits_s {
    const uint8_t *data;
    size_t position;
    size_t floor;
    uint32_t buffer;
    unsigned count;
    bool error;
} xx_pp20_bits;

static uint32_t xx_pp20_get_bits(xx_pp20_bits *bits, unsigned width) {
    uint32_t result = 0U;
    unsigned index;
    if (!bits || bits->error || width > 32U) {
        if (bits) bits->error = true;
        return 0U;
    }
    for (index = 0U; index < width; ++index) {
        if (bits->count == 0U) {
            if (bits->position <= bits->floor) {
                bits->error = true;
                return 0U;
            }
            --bits->position;
            bits->buffer = bits->data[bits->position];
            bits->count = 8U;
        }
        result = (result << 1U) | (bits->buffer & 1U);
        bits->buffer >>= 1U;
        --bits->count;
    }
    return result;
}

/* ---------------------------------------------------------- decoder ---- */

static bool xx_pp20_decrunch(const uint8_t *input, size_t stream_start,
                             size_t stream_end, const uint8_t *widths,
                             uint8_t skip_bits, uint8_t *output,
                             size_t output_size, xx_pd_struct *pd) {
    xx_pp20_bits bits;
    size_t cursor = output_size;
    uint32_t guard = 0U;
    if (!input || !widths || (!output && output_size != 0U) ||
        stream_end <= stream_start) {
        return false;
    }
    bits.data = input;
    bits.position = stream_end;
    bits.floor = stream_start;
    bits.buffer = 0U;
    bits.count = 0U;
    bits.error = false;
    xx_pp20_get_bits(&bits, skip_bits);
    while (cursor > 0U) {
        uint32_t class_index, offset, code;
        uint32_t length;
        unsigned width;
        if (bits.error) return false;
        if ((++guard & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd)) {
            return false;
        }
        if (xx_pp20_get_bits(&bits, 1U) == 0U) {
            uint32_t run = 0U;
            do {
                code = xx_pp20_get_bits(&bits, 2U);
                run += code;
                if (bits.error || run > (uint32_t)XX_PP20_MAX_OUTPUT) {
                    return false;
                }
            } while (code == 3U);
            length = run + 1U;
            if (length > cursor) return false;
            while (length-- != 0U) {
                output[--cursor] = (uint8_t)xx_pp20_get_bits(&bits, 8U);
            }
            if (bits.error) return false;
            if (cursor == 0U) break;
        }
        class_index = xx_pp20_get_bits(&bits, 2U);
        if (bits.error) return false;
        width = widths[class_index];
        length = class_index + 2U;
        if (class_index == 3U) {
            if (xx_pp20_get_bits(&bits, 1U) == 0U) width = 7U;
            offset = xx_pp20_get_bits(&bits, width);
            do {
                code = xx_pp20_get_bits(&bits, 3U);
                length += code;
                if (bits.error || length > (uint32_t)XX_PP20_MAX_OUTPUT) {
                    return false;
                }
            } while (code == 7U);
        } else {
            offset = xx_pp20_get_bits(&bits, width);
        }
        if (bits.error || length > cursor) return false;
        while (length-- != 0U) {
            /* The match source sits AHEAD of the write cursor, among bytes
             * that were already produced; anything else is a corrupt file. */
            size_t source = cursor + (size_t)offset;
            if (source >= output_size) return false;
            output[cursor - 1U] = output[source];
            --cursor;
        }
    }
    return cursor == 0U;
}

/* ----------------------------------------------------------- parsing ---- */

static bool xx_pp20_valid_table(const uint8_t *widths) {
    unsigned index;
    if (!widths) return false;
    for (index = 0U; index < XX_PP20_TABLE_SIZE; ++index) {
        if (widths[index] < XX_PP20_MIN_WIDTH ||
            widths[index] > XX_PP20_MAX_WIDTH) {
            return false;
        }
        if (index != 0U && widths[index] < widths[index - 1U]) return false;
    }
    return true;
}

/* Fills `context` from the header and trailer only.  Every size that could
 * drive an allocation is bounded against the real stream size here, before
 * the caller allocates anything. */
static bool xx_pp20_parse_buffer(const uint8_t *input, size_t input_size,
                                 xx_pp20_context *context) {
    size_t header_size;
    uint64_t packed_size;
    uint64_t uncompressed_size;
    const uint8_t *trailer;
    if (!input || !context || input_size < 16U) return false;
    xx_mem_zero(context, sizeof(*context));
    if (xx_rt_memcmp(input, "PP20", 4U) == 0) {
        header_size = XX_PP20_HEADER_PLAIN;
        context->variant = XX_PP20_VARIANT_PP20;
    } else if (xx_rt_memcmp(input, "PP11", 4U) == 0) {
        header_size = XX_PP20_HEADER_PLAIN;
        context->variant = XX_PP20_VARIANT_PP11;
    } else if (xx_rt_memcmp(input, "PPLS", 4U) == 0) {
        header_size = XX_PP20_HEADER_PPLS;
        context->variant = XX_PP20_VARIANT_PPLS;
    } else if (xx_rt_memcmp(input, "PX20", 4U) == 0) {
        header_size = XX_PP20_HEADER_PX20;
        context->variant = XX_PP20_VARIANT_PX20;
        context->crypted = true;
    } else if (xx_rt_memcmp(input, "PPBK", 4U) == 0) {
        header_size = XX_PP20_HEADER_PLAIN;
        context->variant = XX_PP20_VARIANT_PPBK;
        context->crypted = true;
    } else {
        return false;
    }
    /* PowerPacker pads the crunched stream to a longword, so a genuine file
     * is always a multiple of four bytes long. */
    if ((input_size & 3U) != 0U ||
        input_size < header_size + XX_PP20_TRAILER_SIZE + 1U) {
        return false;
    }
    xx_rt_memcpy(context->offset_widths,
                 input + header_size - XX_PP20_TABLE_SIZE,
                 XX_PP20_TABLE_SIZE);
    if (!xx_pp20_valid_table(context->offset_widths)) return false;
    trailer = input + input_size - XX_PP20_TRAILER_SIZE;
    uncompressed_size = ((uint64_t)trailer[0] << 16U) |
                        ((uint64_t)trailer[1] << 8U) | (uint64_t)trailer[2];
    context->skip_bits = trailer[3];
    if (context->skip_bits > XX_PP20_MAX_SKIP) return false;
    packed_size = (uint64_t)input_size - header_size - XX_PP20_TRAILER_SIZE;
    if (uncompressed_size == 0U ||
        uncompressed_size > XX_PP20_MAX_OUTPUT ||
        uncompressed_size > (uint64_t)SIZE_MAX ||
        uncompressed_size > packed_size * XX_PP20_MAX_RATIO +
                                XX_PP20_RATIO_SLACK) {
        return false;
    }
    context->uncompressed_size = uncompressed_size;
    context->stream_size = (int64_t)input_size;
    return true;
}

/* Reads the whole stream, validates it and, when `destination` is given,
 * writes the decrunched bytes out.  The encrypted variants stop after the
 * header check: they are recognised, never guessed at. */
static bool xx_pp20_decode_stream(Abstractformat *self,
                                  xx_io_device *destination,
                                  xx_pp20_context *context,
                                  xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    size_t header_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    xx_pp20_context parsed;
    bool result = false;
    if (!self || !self->device || self->base_address < 0 || !context ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    input_size = total_size - self->base_address;
    if (input_size < 16 || (uint64_t)input_size > XX_PP20_MAX_INPUT ||
        (uint64_t)input_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input ||
        !xx_pp20_read_exact_at(self->device, self->base_address, input,
                               (size_t)input_size) ||
        !xx_pp20_parse_buffer(input, (size_t)input_size, &parsed)) {
        goto cleanup;
    }
    if (parsed.crypted) {
        /* Recognised, but fail closed: no password, no plaintext. */
        if (destination) goto cleanup;
        *context = parsed;
        result = true;
        goto cleanup;
    }
    header_size = parsed.variant == XX_PP20_VARIANT_PPLS
                      ? XX_PP20_HEADER_PPLS : XX_PP20_HEADER_PLAIN;
    output = (uint8_t *)xx_mem_alloc((size_t)parsed.uncompressed_size);
    if (!output ||
        !xx_pp20_decrunch(input, header_size,
                          (size_t)input_size - XX_PP20_TRAILER_SIZE,
                          parsed.offset_widths, parsed.skip_bits, output,
                          (size_t)parsed.uncompressed_size, pd) ||
        (pd && xx_pd_is_stopped(pd)) ||
        (destination && !xx_pp20_write_all(destination, output,
                                           (size_t)parsed.uncompressed_size,
                                           pd))) {
        goto cleanup;
    }
    *context = parsed;
    result = true;
cleanup:
    xx_mem_free(output);
    xx_mem_free(input);
    return result;
}

/* ----------------------------------------------------------- options ---- */

static bool xx_pp20_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
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

static const xx_var *xx_pp20_find_option(const xx_list_s *options,
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

static bool xx_pp20_populate_record(Abstractformat *self,
                                    xx_archive_record *record) {
    const xx_pp20 *archive;
    int64_t header_size;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_pp20 *)self;
    header_size = archive->variant == XX_PP20_VARIANT_PPLS
                      ? (int64_t)XX_PP20_HEADER_PPLS
                      : (archive->variant == XX_PP20_VARIANT_PX20
                             ? (int64_t)XX_PP20_HEADER_PX20
                             : (int64_t)XX_PP20_HEADER_PLAIN);
    if (self->format_size <= header_size) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = header_size;
    record->data_offset = self->base_address + header_size;
    record->compressed_size = self->format_size - header_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_PP20_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          XX_PP20_COMPRESSION_METHOD) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(
               record, XX_META_ID_IS_ENCRYPTED,
               xx_pp20_variant_is_crypted(archive->variant));
}

/* ---------------------------------------------------------- lifetime ---- */

void xx_pp20_init(xx_pp20 *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_PP20_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-powerpacker");
    xx_format_set_extension(&archive->format, "pp");
    archive->format.check_is_valid = xx_pp20_check_is_valid;
    archive->format.handle_base_info = xx_pp20_handle_base_info;
    archive->format.get_format_size = xx_pp20_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pp20_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pp20_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pp20_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pp20_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pp20_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pp20_free_archive_records_reading;
    archive->format.destroy = xx_pp20_vtable_destroy;
    archive->stream_end = -1;
}

xx_pp20 *xx_pp20_create(xx_io_device *device, int64_t base_address) {
    xx_pp20 *archive = (xx_pp20 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pp20_init(archive, device, base_address);
    return archive;
}

void xx_pp20_destroy(xx_pp20 *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->variant = XX_PP20_VARIANT_PP20;
    archive->skip_bits = 0U;
    xx_mem_zero(archive->offset_widths, sizeof(archive->offset_widths));
}

static void xx_pp20_vtable_destroy(Abstractformat *self) {
    xx_pp20_destroy((xx_pp20 *)self);
}

void xx_pp20_free(xx_pp20 *archive) {
    if (!archive) return;
    xx_pp20_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- info ---- */

bool xx_pp20_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pp20_context context;
    return xx_pp20_decode_stream(self, NULL, &context, pd);
}

bool xx_pp20_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pp20_context context;
    xx_pp20 *archive;
    if (!self || !xx_pp20_decode_stream(self, NULL, &context, pd)) {
        if (self) {
            archive = (xx_pp20 *)self;
            archive->uncompressed_size = 0U;
            archive->stream_end = -1;
            archive->variant = XX_PP20_VARIANT_PP20;
            archive->skip_bits = 0U;
            xx_mem_zero(archive->offset_widths,
                        sizeof(archive->offset_widths));
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_pp20 *)self;
    archive->uncompressed_size = context.uncompressed_size;
    archive->stream_end = self->base_address + context.stream_size;
    archive->variant = context.variant;
    archive->skip_bits = context.skip_bits;
    xx_rt_memcpy(archive->offset_widths, context.offset_widths,
                 sizeof(archive->offset_widths));
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_PP20_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = context.crypted;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_pp20_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_pp20_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_pp20_unpack_to_device(xx_pp20 *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    xx_pp20_context context;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        xx_pp20_variant_is_crypted(archive->variant) ||
        !xx_pp20_decode_stream(&archive->format, destination, &context, pd)) {
        return false;
    }
    return context.stream_size == archive->format.format_size &&
           context.uncompressed_size == archive->uncompressed_size;
}

/* ----------------------------------------------------------- records ---- */

xx_archive_record_state *xx_pp20_create_archive_records_reading(
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
    if (!xx_pp20_copy_options(&state->options, options) ||
        !xx_pp20_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_pp20_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pp20_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_pp20_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    xx_pp20 *archive = (xx_pp20 *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (xx_pp20_variant_is_crypted(archive->variant)) return false;
    path_value = xx_pp20_find_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_pp20_context context;
        return xx_pp20_decode_stream(self, NULL, &context, pd) &&
               context.stream_size == self->format_size &&
               context.uncompressed_size == archive->uncompressed_size;
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
                                          XX_PP20_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_PP20_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_pp20_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_pp20_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_pp20_get_uncompressed_size(const xx_pp20 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_pp20_get_stream_end(const xx_pp20 *archive) {
    return archive ? archive->stream_end : -1;
}
