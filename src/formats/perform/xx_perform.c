/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Delrina PerFORM / FormFlow "compressed database" document.  A 34-byte
 * NUL-terminated banner, a u16 CRC-16/ARC of the decoded document, then one
 * LZW code stream to end of file.  Both the container and the codec were
 * recovered from the original unpacker's own code and verified against all
 * 782 corpus samples using the stored CRC; xx_perform.h records the layout,
 * the dialect and the evidence behind each field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/perform/xx_perform.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PERFORM is registered there. */
#ifdef PERFORM
#define XX_PERFORM_FILE_TYPE XX_FILE_TYPE_PERFORM
#else
#define XX_PERFORM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The file stores no member name; one is synthesised for the single record. */
#define XX_PERFORM_PAYLOAD_NAME "database"
/* Size of the staging buffer used when flushing decoded bytes. */
#define XX_PERFORM_FLUSH_SIZE 4096U

typedef struct xx_perform_parsed_s {
    int64_t total_size;    /**< Device size, the bound every field is checked
                            *   against. */
    int64_t packed_offset; /**< Absolute offset of the packed stream. */
    int64_t packed_size;   /**< Packed stream length, derived from the real
                            *   file size - the format declares none. */
    uint16_t checksum;     /**< CRC-16/ARC of the decoded document. */
} xx_perform_parsed;

/* Everything the decode produces that the caller may want back. */
typedef struct xx_perform_decoded_s {
    uint64_t produced;                        /**< Decoded byte count. */
    uint16_t crc;                             /**< CRC-16/ARC of those bytes. */
    char banner[XX_PERFORM_BANNER_SIZE + 1U]; /**< Decoded self-description. */
} xx_perform_decoded;

static void xx_perform_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a document can sit at an arbitrary
 * offset inside a larger dump, and long is 32-bit on Win64. */
static bool xx_perform_read_at(xx_io_device *device, int64_t offset,
                               void *data, size_t size) {
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

static bool xx_perform_write_all(xx_io_device *device, const void *data,
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

/* ----------------------------------------------------------------- lzw -- */

/* Least-significant-bit-first reader over the packed stream. */
typedef struct xx_perform_bits_s {
    const uint8_t *data;
    size_t size;
    size_t pos;       /**< Next byte to pull into the accumulator. */
    uint32_t hold;    /**< Bits not yet consumed, lowest first. */
    unsigned count;   /**< How many of them are valid. */
} xx_perform_bits;

/* Reads @p width bits, or reports exhaustion.  Every caller checks the
 * return: a truncated code is a broken stream, never a zero-filled one. */
static bool xx_perform_bits_get(xx_perform_bits *bits, unsigned width,
                                uint32_t *value) {
    if (!bits || !value || width == 0U || width > 16U) return false;
    while (bits->count < width) {
        if (bits->pos >= bits->size) return false;
        bits->hold |= (uint32_t)bits->data[bits->pos] << bits->count;
        ++bits->pos;
        bits->count += 8U;
    }
    *value = bits->hold & (((uint32_t)1 << width) - 1U);
    bits->hold >>= width;
    bits->count -= width;
    return true;
}

/* The dictionary, the reversal stack and the flush buffer together are 36 KB,
 * which is more than belongs on a stack frame, so the decoder owns one heap
 * block instead. */
typedef struct xx_perform_lzw_s {
    uint16_t prefix[XX_PERFORM_MAX_CODES];
    uint8_t suffix[XX_PERFORM_MAX_CODES];
    uint8_t stack[XX_PERFORM_MAX_CODES];
    uint8_t flush[XX_PERFORM_FLUSH_SIZE];
} xx_perform_lzw;

/* State carried across the emit helper. */
typedef struct xx_perform_sink_s {
    xx_io_device *destination; /**< NULL to validate and measure only. */
    uint8_t *flush;
    size_t flush_used;
    uint64_t total;
    uint64_t limit;
    uint16_t crc;
    char banner[XX_PERFORM_BANNER_SIZE + 1U];
    size_t banner_used;
} xx_perform_sink;

/* Takes one decoded byte: counts it, folds it into the CRC, keeps the opening
 * banner and stages it for output. */
static bool xx_perform_emit(xx_perform_sink *sink, uint8_t byte,
                            xx_pd_struct *pd) {
    if (sink->total >= sink->limit) return false;
    ++sink->total;
    sink->crc = xx_crc16_arc_calc(sink->crc, &byte, 1U);
    if (sink->banner_used < XX_PERFORM_BANNER_SIZE) {
        sink->banner[sink->banner_used] = (char)byte;
        ++sink->banner_used;
    }
    sink->flush[sink->flush_used] = byte;
    ++sink->flush_used;
    if (sink->flush_used == XX_PERFORM_FLUSH_SIZE) {
        if (sink->destination &&
            !xx_perform_write_all(sink->destination, sink->flush,
                                  sink->flush_used, pd)) {
            return false;
        }
        sink->flush_used = 0U;
    }
    return true;
}

/* Pushes the phrase named by @p code onto the reversal stack and emits it
 * least-recent-first.  @p last receives the phrase's first byte, which is
 * both the suffix of the next dictionary entry and the byte the KwKwK case
 * needs.  A prefix chain is strictly decreasing and so cannot exceed the
 * table size; the bound is still enforced rather than assumed. */
static bool xx_perform_emit_phrase(xx_perform_lzw *lzw, xx_perform_sink *sink,
                                   uint32_t code, uint32_t previous,
                                   uint32_t next_free, uint8_t *last,
                                   xx_pd_struct *pd) {
    size_t depth = 0U;
    uint32_t walk = code;

    if (code >= next_free) {
        /* KwKwK: the code names the entry that this token is about to
         * create, so the phrase is the previous one plus its own first
         * byte. */
        lzw->stack[depth] = *last;
        ++depth;
        walk = previous;
    }
    while (walk > 0xFFU) {
        if (walk >= XX_PERFORM_MAX_CODES || depth >= XX_PERFORM_MAX_CODES) {
            return false;
        }
        lzw->stack[depth] = lzw->suffix[walk];
        ++depth;
        walk = lzw->prefix[walk];
    }
    if (depth >= XX_PERFORM_MAX_CODES) return false;
    lzw->stack[depth] = (uint8_t)walk;
    ++depth;
    *last = (uint8_t)walk;
    while (depth != 0U) {
        --depth;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_perform_emit(sink, lzw->stack[depth], pd)) return false;
    }
    return true;
}

/* Decodes the whole stream.  @p destination may be NULL to validate and
 * measure without producing output.  Output is flushed as it is produced, so
 * the peak allocation is the packed stream plus the fixed 36 KB working set.
 */
static bool xx_perform_lzw_decode(const uint8_t *packed, size_t packed_size,
                                  xx_io_device *destination, uint64_t limit,
                                  xx_perform_decoded *result,
                                  xx_pd_struct *pd) {
    xx_perform_lzw *lzw;
    xx_perform_sink sink;
    xx_perform_bits bits;
    uint32_t index;
    uint32_t code = 0U;
    uint32_t previous = 0U;
    uint32_t next_free = XX_PERFORM_FIRST_CODE;
    uint32_t table_limit = 0x1FFU;
    unsigned width = 9U;
    uint8_t last = 0U;
    bool ended = false;
    bool ok = true;

    if (!packed || packed_size == 0U || !result) return false;
    xx_mem_zero(result, sizeof(*result));
    lzw = (xx_perform_lzw *)xx_mem_alloc(sizeof(*lzw));
    if (!lzw) return false;
    xx_mem_zero(lzw, sizeof(*lzw));
    for (index = 0U; index < 0x100U; ++index) {
        lzw->prefix[index] = 0U;
        lzw->suffix[index] = (uint8_t)index;
    }

    xx_mem_zero(&sink, sizeof(sink));
    sink.destination = destination;
    sink.flush = lzw->flush;
    sink.limit = limit;
    bits.data = packed;
    bits.size = packed_size;
    bits.pos = 0U;
    bits.hold = 0U;
    bits.count = 0U;

    /* The stream must open with CLEAR.  The original relies on that too, but
     * by omission: without it, it decodes against uninitialised state.  Here
     * a stream that does not is simply not this codec. */
    if (!xx_perform_bits_get(&bits, width, &code) ||
        code != XX_PERFORM_CLEAR_CODE) {
        xx_mem_free(lzw);
        return false;
    }

    while (ok && !ended) {
        /* Fresh table: width and first free slot back to the start, and the
         * next code has to be a literal that seeds `previous`. */
        width = 9U;
        table_limit = 0x1FFU;
        next_free = XX_PERFORM_FIRST_CODE;
        if (!xx_perform_bits_get(&bits, width, &code)) {
            ok = false;
            break;
        }
        if (code == XX_PERFORM_END_CODE || code == XX_PERFORM_CLEAR_CODE) {
            /* An immediate END, or an empty table restated, ends the stream
             * exactly as the original does. */
            ended = true;
            break;
        }
        if (code > 0xFFU) {
            ok = false;
            break;
        }
        previous = code;
        last = (uint8_t)code;
        if (!xx_perform_emit(&sink, last, pd)) {
            ok = false;
            break;
        }

        for (;;) {
            if (pd && xx_pd_is_stopped(pd)) {
                ok = false;
                break;
            }
            /* Widen before reading, once the next free slot would no longer
             * fit.  At the top width the table caps instead of widening. */
            if (table_limit < next_free && width < XX_PERFORM_MAX_BITS) {
                ++width;
                table_limit = (width == XX_PERFORM_MAX_BITS)
                                  ? XX_PERFORM_MAX_CODES
                                  : (((uint32_t)1 << width) - 1U);
            }
            if (!xx_perform_bits_get(&bits, width, &code)) {
                ok = false;
                break;
            }
            if (code == XX_PERFORM_END_CODE) {
                ended = true;
                break;
            }
            if (code == XX_PERFORM_CLEAR_CODE) break; /* restart the table */
            if (!xx_perform_emit_phrase(lzw, &sink, code, previous, next_free,
                                        &last, pd)) {
                ok = false;
                break;
            }
            if (next_free < XX_PERFORM_MAX_CODES) {
                lzw->prefix[next_free] = (uint16_t)previous;
                lzw->suffix[next_free] = last;
                ++next_free;
            }
            previous = code;
        }
    }

    if (ok && ended && sink.total != 0U) {
        if (destination && sink.flush_used != 0U &&
            !xx_perform_write_all(destination, sink.flush, sink.flush_used,
                                  pd)) {
            ok = false;
        }
    } else {
        ok = false;
    }

    if (ok) {
        result->produced = sink.total;
        result->crc = sink.crc;
        /* The banner is only reported when the whole of it is printable; a
         * partial or binary opening says nothing and is left empty. */
        if (sink.banner_used == XX_PERFORM_BANNER_SIZE) {
            size_t at;
            bool printable = true;
            for (at = 0U; at < XX_PERFORM_BANNER_SIZE; ++at) {
                uint8_t byte = (uint8_t)sink.banner[at];
                if (byte < 0x20U || byte > 0x7EU) printable = false;
            }
            if (printable) {
                for (at = 0U; at < XX_PERFORM_BANNER_SIZE; ++at) {
                    result->banner[at] = sink.banner[at];
                }
                result->banner[XX_PERFORM_BANNER_SIZE] = '\0';
            }
        }
    }
    xx_mem_free(lzw);
    return ok;
}

/* Reads the packed extent and runs it through the decoder. */
static bool xx_perform_decode_stream(const xx_perform_parsed *parsed,
                                     xx_io_device *device,
                                     xx_io_device *destination,
                                     xx_perform_decoded *result,
                                     xx_pd_struct *pd) {
    uint8_t *packed;
    uint64_t limit;
    bool ok;

    if (!parsed || !device || !result) return false;
    xx_mem_zero(result, sizeof(*result));
    if (parsed->packed_size <= 0 ||
        parsed->packed_size > XX_PERFORM_MAX_PACKED_SIZE ||
        (uint64_t)parsed->packed_size > (uint64_t)(size_t)-1) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)parsed->packed_size);
    if (!packed) return false;
    if (!xx_perform_read_at(device, parsed->packed_offset, packed,
                            (size_t)parsed->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    /* Bounded before a single byte is produced, so a 36-byte header can never
     * drive an unbounded loop: the ratio cap and the absolute cap, whichever
     * bites first. */
    limit = (uint64_t)parsed->packed_size * (uint64_t)XX_PERFORM_MAX_EXPANSION;
    if (limit > XX_PERFORM_MAX_UNPACKED_SIZE) {
        limit = XX_PERFORM_MAX_UNPACKED_SIZE;
    }
    ok = xx_perform_lzw_decode(packed, (size_t)parsed->packed_size,
                               destination, limit, result, pd);
    xx_mem_free(packed);
    /* The file asserts a CRC-16/ARC over the decoded bytes; without that
     * match there is no way to tell a correct decode from a plausible one, so
     * a mismatch is a failure rather than a warning. */
    if (ok && result->crc != parsed->checksum) ok = false;
    if (!ok) xx_mem_zero(result, sizeof(*result));
    return ok;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_perform_parse(Abstractformat *self, xx_perform_parsed *parsed,
                             xx_pd_struct *pd) {
    uint8_t header[XX_PERFORM_HEADER_SIZE];
    int64_t span;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->total_size = -1;
        parsed->packed_offset = -1;
        parsed->packed_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }

    parsed->total_size = xx_io_total_size(self->device);
    if (parsed->total_size < 0 || parsed->total_size < self->base_address) {
        return false;
    }
    span = parsed->total_size - self->base_address;
    /* The header itself plus at least one payload byte.  A document with an
     * empty code stream has never been seen and would decode to nothing. */
    if (span <= (int64_t)XX_PERFORM_HEADER_SIZE) return false;

    if (!xx_perform_read_at(self->device, self->base_address, header,
                            sizeof(header))) {
        return false;
    }
    /* The banner and its NUL terminator. */
    if (xx_rt_memcmp(header, XX_PERFORM_SIGNATURE,
                     XX_PERFORM_SIGNATURE_SIZE) != 0) {
        return false;
    }

    parsed->checksum = (uint16_t)((uint16_t)header[34] |
                                  ((uint16_t)header[35] << 8U));
    parsed->packed_offset = self->base_address + (int64_t)XX_PERFORM_HEADER_SIZE;
    /* The payload length is the measured remainder of the file, not a
     * declared field - the format stores none.  span > HEADER_SIZE was
     * checked above, so this is positive and inside the device. */
    parsed->packed_size = span - (int64_t)XX_PERFORM_HEADER_SIZE;
    return true;
}

/* ------------------------------------------------------------- options -- */

static bool xx_perform_copy_options(xx_list_s *destination,
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

static const xx_var *xx_perform_find_option(const xx_list_s *options,
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

static bool xx_perform_populate_record(Abstractformat *self,
                                       xx_archive_record *record) {
    const xx_perform *document;

    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    document = (const xx_perform *)self;
    if (document->packed_offset < 0 || document->packed_size <= 0) {
        return false;
    }
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)XX_PERFORM_HEADER_SIZE;
    record->data_offset = document->packed_offset;
    record->compressed_size = document->packed_size;
    if (!xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                        XX_PERFORM_PAYLOAD_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        document->uncompressed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)document->packed_size) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         false) ||
        /* The packed stream is never encrypted; when the document inside it
         * is, its banner says so and that is all this reader claims. */
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    if (document->banner[0] != '\0' &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                        document->banner)) {
        return false;
    }
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_perform_init(xx_perform *document, xx_io_device *device,
                     int64_t base_address) {
    if (!document) return;
    xx_mem_zero(document, sizeof(*document));
    xx_format_init(&document->format, device, base_address);
    document->format.endian = XX_ENDIAN_LITTLE;
    document->format.file_type = XX_PERFORM_FILE_TYPE;
    document->format.format_type = XX_TYPE_ARCHIVE;
    document->format.is_archive = true;
    xx_format_set_mime_type(&document->format,
                            "application/x-delrina-perform");
    xx_format_set_extension(&document->format, "frp");
    xx_format_set_version(&document->format, "1.00");
    document->format.check_is_valid = xx_perform_check_is_valid;
    document->format.handle_base_info = xx_perform_handle_base_info;
    document->format.get_format_size = xx_perform_get_format_size;
    document->format.get_number_of_archive_records =
        xx_perform_get_number_of_archive_records;
    document->format.create_archive_records_reading =
        xx_perform_create_archive_records_reading;
    document->format.get_current_archive_record =
        xx_perform_get_current_archive_record;
    document->format.unpack_current_archive_record =
        xx_perform_unpack_current_archive_record;
    document->format.archive_record_move_to_next =
        xx_perform_archive_record_move_to_next;
    document->format.free_archive_records_reading =
        xx_perform_free_archive_records_reading;
    document->format.destroy = xx_perform_vtable_destroy;
    document->packed_offset = -1;
    document->packed_size = -1;
    document->uncompressed_size = 0U;
    document->checksum = 0U;
    document->checksum_valid = false;
    document->unpackable = false;
    document->banner[0] = '\0';
}

xx_perform *xx_perform_create(xx_io_device *device, int64_t base_address) {
    xx_perform *document = (xx_perform *)xx_mem_alloc(sizeof(*document));

    if (document) xx_perform_init(document, device, base_address);
    return document;
}

void xx_perform_destroy(xx_perform *document) {
    if (!document) return;
    if (document->format.close) document->format.close(&document->format);
    xx_format_cleanup_extra_parameters(&document->format);
    document->packed_offset = -1;
    document->packed_size = -1;
    document->uncompressed_size = 0U;
    document->checksum = 0U;
    document->checksum_valid = false;
    document->unpackable = false;
    document->banner[0] = '\0';
}

static void xx_perform_vtable_destroy(Abstractformat *self) {
    xx_perform_destroy((xx_perform *)self);
}

void xx_perform_free(xx_perform *document) {
    if (!document) return;
    xx_perform_destroy(document);
    xx_mem_free(document);
}

/* -------------------------------------------------------------- format -- */

bool xx_perform_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_perform_parsed parsed;

    return xx_perform_parse(self, &parsed, pd);
}

bool xx_perform_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_perform *document = (xx_perform *)self;
    xx_perform_parsed parsed;
    xx_perform_decoded decoded;
    size_t index;

    if (!self) return false;
    if (!xx_perform_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    document->checksum = parsed.checksum;
    document->packed_offset = parsed.packed_offset;
    document->packed_size = parsed.packed_size;
    /* The stream declares no decoded length, so it is measured by decoding.
     * A stream that will not decode, or that decodes to something the stored
     * CRC does not confirm, still leaves a recognised file: the record is
     * published with an unknown size and unpacking it fails. */
    document->unpackable = xx_perform_decode_stream(&parsed, self->device,
                                                    NULL, &decoded, pd);
    document->checksum_valid = document->unpackable;
    document->uncompressed_size = document->unpackable ? decoded.produced : 0U;
    document->banner[0] = '\0';
    if (document->unpackable) {
        for (index = 0U; index <= XX_PERFORM_BANNER_SIZE; ++index) {
            document->banner[index] = decoded.banner[index];
        }
    }
    /* Header plus payload, and the payload was measured to the end of the
     * device, so there is no overlay to report. */
    self->format_size = parsed.total_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_perform_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_perform_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

/* -------------------------------------------------------------- unpack -- */

bool xx_perform_unpack_to_device(xx_perform *document,
                                 xx_io_device *destination,
                                 xx_pd_struct *pd) {
    xx_perform_parsed parsed;
    xx_perform_decoded decoded;

    if (!document ||
        (!document->format.base_info_handled &&
         !xx_format_handle_base_info(&document->format, pd)) ||
        !document->format.is_valid || !document->unpackable) {
        return false;
    }
    if (!xx_perform_parse(&document->format, &parsed, pd)) return false;
    if (!xx_perform_decode_stream(&parsed, document->format.device,
                                  destination, &decoded, pd)) {
        return false;
    }
    return decoded.produced == document->uncompressed_size;
}

xx_archive_record_state *xx_perform_create_archive_records_reading(
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
    if (!xx_perform_copy_options(&state->options, options) ||
        !xx_perform_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_perform_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_perform_archive_record_move_to_next(Abstractformat *self,
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

bool xx_perform_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_perform *document = (xx_perform *)self;
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    size_t path_length;
    bool result;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value =
        xx_perform_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        /* No destination: verify the stream decodes, produce nothing. */
        return xx_perform_unpack_to_device(document, NULL, pd);
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
    path_length = xx_str_len(base_path);
    if (path_length != 0U && base_path[path_length - 1U] != '/' &&
        base_path[path_length - 1U] != '\\') {
        destination_path =
            xx_str_concat3(base_path, "/", XX_PERFORM_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_PERFORM_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_perform_unpack_to_device(document, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_perform_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

uint16_t xx_perform_get_checksum(const xx_perform *document) {
    return document ? document->checksum : 0U;
}

bool xx_perform_is_checksum_valid(const xx_perform *document) {
    return document ? document->checksum_valid : false;
}

int64_t xx_perform_get_packed_offset(const xx_perform *document) {
    return document ? document->packed_offset : -1;
}

int64_t xx_perform_get_packed_size(const xx_perform *document) {
    return document ? document->packed_size : -1;
}

uint64_t xx_perform_get_uncompressed_size(const xx_perform *document) {
    return document ? document->uncompressed_size : 0U;
}

const char *xx_perform_get_banner(const xx_perform *document) {
    return document ? document->banner : "";
}
