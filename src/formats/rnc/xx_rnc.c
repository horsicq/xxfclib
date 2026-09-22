/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for Rob Northen Compression (RNC ProPack) streams.
 *
 * The file is a single packed stream introduced by an 18 byte big-endian
 * header:
 *
 *   0  char[3]  "RNC"
 *   3  uint8    method, 1 (LZ77 + Huffman) or 2 (bitwise LZ77)
 *   4  uint32   unpacked size
 *   8  uint32   packed size
 *  12  uint16   CRC16 of the unpacked data
 *  14  uint16   CRC16 of the packed data
 *  16  uint8    leeway (bytes needed for unpacking in place)
 *  17  uint8    number of packed chunks
 *
 * The CRC is the reflected CRC16 with polynomial 0xA001 and a zero seed, the
 * one the vendor unpackers build in their crc_block routine.  The packed data
 * follows the header directly and the stream ends at 18 + packed size.
 *
 * Bullfrog's game data files put an eight byte "BULLFROG" tag in front of an
 * otherwise ordinary stream.  The tag carries no fields; it is skipped, and
 * counted in the format size so the reader still describes the whole file.
 * This is NOT the multi-member "RNCA" container, which has its own reader.
 *
 * Both methods were taken from the vendor unpack sources shipped with RNC
 * ProPack 2.14 (SOURCE/MC68000/RNC_1.S and RNC_2.S) and verified against the
 * corpus: every unencrypted sample reproduces the header CRC16 of the decoded
 * data.  Streams packed with the "K" (key) option decode structurally but
 * carry key-XORed literals; they are reported as encrypted instead of being
 * rejected, because no key is available to confirm the unpacked CRC.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rnc/xx_rnc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef RNC
#define XX_RNC_FILE_TYPE XX_FILE_TYPE_RNC
#else
#define XX_RNC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_RNC_PAYLOAD_NAME "payload"
#define XX_RNC_HEADER_SIZE 18U
/* Length of the "BULLFROG" tag some game data files put in front of the
 * stream.  It carries no fields of its own. */
#define XX_RNC_BULLFROG_SIZE 8U
/* Hard ceiling on a declared unpacked size, and on how much a declared
 * unpacked size may exceed the packed bytes actually present.  The densest
 * sample in the corpus expands by a factor of 491. */
#define XX_RNC_MAX_OUTPUT ((uint64_t)256U * 1024U * 1024U)
#define XX_RNC_MAX_RATIO ((uint64_t)8192U)
/* Both methods may write a few bytes past the declared end while finishing
 * the last block; those bytes are discarded. */
#define XX_RNC_OVERSHOOT ((uint64_t)0x10000U)
#define XX_RNC_MAX_BIT_READ 16U

typedef struct xx_rnc_header_s {
    uint64_t unpacked_size;
    uint64_t packed_size;
    uint16_t unpacked_crc;
    uint16_t packed_crc;
    uint8_t method;
    uint8_t leeway;
    uint8_t chunk_count;
    int64_t prefix_size; /* bytes before the "RNC" tag (Bullfrog wrapper) */
    int64_t stream_size; /* prefix + header + packed data */
    int64_t total_size;  /* bytes available from base_address */
    bool crc_valid;      /* decoded data matched the declared CRC16 */
} xx_rnc_header;

typedef struct xx_rnc_output_s {
    uint8_t *data;
    uint64_t capacity; /* declared unpacked size */
    uint64_t limit;    /* capacity + overshoot allowance */
    uint64_t position; /* logical position, may exceed capacity */
    bool failed;
} xx_rnc_output;

static void xx_rnc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static bool xx_rnc_read_exact_at(xx_io_device *device, int64_t offset,
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

static bool xx_rnc_write_all(xx_io_device *device, const void *data,
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

static uint16_t xx_rnc_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                             : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static uint32_t xx_rnc_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint16_t xx_rnc_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

static void xx_rnc_output_init(xx_rnc_output *output, uint8_t *data,
                               uint64_t capacity) {
    output->data = data;
    output->capacity = capacity;
    output->limit = capacity + XX_RNC_OVERSHOOT;
    output->position = 0U;
    output->failed = false;
}

/* Bytes past the declared size are counted but dropped: the vendor unpackers
 * rely on a "leeway" area that the caller never sees. */
static void xx_rnc_output_put(xx_rnc_output *output, uint8_t value) {
    if (output->position < output->capacity) {
        output->data[output->position] = value;
    } else if (output->position >= output->limit) {
        output->failed = true;
        return;
    }
    ++output->position;
}

static uint8_t xx_rnc_output_at(const xx_rnc_output *output,
                                uint64_t position) {
    return position < output->capacity ? output->data[position] : 0U;
}

static bool xx_rnc_output_copy(xx_rnc_output *output, uint64_t distance,
                               uint64_t length) {
    uint64_t index;
    if (distance == 0U || distance > output->position) return false;
    for (index = 0U; index < length; ++index) {
        xx_rnc_output_put(output,
                          xx_rnc_output_at(output,
                                           output->position - distance));
        if (output->failed) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* method 1: LZ77 with three Huffman tables per block                  */
/* ------------------------------------------------------------------ */

/* The bit reader keeps between 16 and 31 bits taken from little endian
 * 16 bit words.  `position` is the offset of the most recently loaded word,
 * which is also where a run of raw bytes starts. */
typedef struct xx_rnc_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t buffer;
    unsigned count;
} xx_rnc_bits;

static uint32_t xx_rnc_bits_word(const xx_rnc_bits *bits, size_t position) {
    uint32_t value = 0U;
    if (position < bits->size) value = bits->data[position];
    if (position + 1U < bits->size) {
        value |= (uint32_t)bits->data[position + 1U] << 8;
    }
    return value;
}

static void xx_rnc_bits_init(xx_rnc_bits *bits, const uint8_t *data,
                             size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->buffer = 0U;
    bits->count = 0U;
    bits->buffer = xx_rnc_bits_word(bits, 0U);
    bits->count = 16U;
}

static void xx_rnc_bits_advance(xx_rnc_bits *bits, unsigned amount) {
    bits->buffer >>= amount;
    bits->count -= amount;
    if (bits->count < 16U) {
        bits->position += 2U;
        bits->buffer |= xx_rnc_bits_word(bits, bits->position) << bits->count;
        bits->count += 16U;
    }
}

static uint32_t xx_rnc_bits_peek(const xx_rnc_bits *bits, unsigned amount) {
    return amount == 0U ? 0U
                        : bits->buffer & (uint32_t)((1UL << amount) - 1UL);
}

static uint32_t xx_rnc_bits_read(xx_rnc_bits *bits, unsigned amount) {
    uint32_t value = xx_rnc_bits_peek(bits, amount);
    xx_rnc_bits_advance(bits, amount);
    return value;
}

/* Re-sync after a run of raw bytes: the word that was prefetched is the first
 * word of that run, so it is dropped and the word following the run is
 * loaded instead. */
static void xx_rnc_bits_resync(xx_rnc_bits *bits, size_t position) {
    bits->count -= 16U;
    bits->buffer &= bits->count == 0U
                        ? 0U
                        : (uint32_t)((1UL << bits->count) - 1UL);
    bits->position = position;
    bits->buffer |= xx_rnc_bits_word(bits, bits->position) << bits->count;
    bits->count += 16U;
}

typedef struct xx_rnc_huf_entry_s {
    uint32_t code;
    unsigned length;
    unsigned value;
} xx_rnc_huf_entry;

typedef struct xx_rnc_huf_s {
    unsigned count;
    xx_rnc_huf_entry items[32];
} xx_rnc_huf;

static uint32_t xx_rnc_mirror(uint32_t value, unsigned length) {
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < length; ++index) {
        result = (result << 1) | (value & 1U);
        value >>= 1;
    }
    return result;
}

static bool xx_rnc_read_huftable(xx_rnc_huf *table, xx_rnc_bits *bits) {
    unsigned lengths[32];
    unsigned number, index, level, longest = 1U;
    uint32_t code = 0U;
    table->count = 0U;
    number = (unsigned)xx_rnc_bits_read(bits, 5);
    if (number == 0U) return true;
    if (number > 32U) return false;
    for (index = 0U; index < number; ++index) {
        lengths[index] = (unsigned)xx_rnc_bits_read(bits, 4);
        if (lengths[index] > longest) longest = lengths[index];
    }
    for (level = 1U; level <= longest; ++level) {
        for (index = 0U; index < number; ++index) {
            if (lengths[index] != level) continue;
            table->items[table->count].code = xx_rnc_mirror(code, level);
            table->items[table->count].length = level;
            table->items[table->count].value = index;
            ++table->count;
            ++code;
        }
        code <<= 1;
    }
    return true;
}

static bool xx_rnc_huf_read(const xx_rnc_huf *table, xx_rnc_bits *bits,
                            uint32_t *result) {
    unsigned index;
    for (index = 0U; index < table->count; ++index) {
        uint32_t value;
        if (xx_rnc_bits_peek(bits, table->items[index].length) !=
            table->items[index].code) {
            continue;
        }
        xx_rnc_bits_advance(bits, table->items[index].length);
        value = table->items[index].value;
        if (value >= 2U) {
            unsigned extra = value - 1U;
            if (extra > XX_RNC_MAX_BIT_READ) return false;
            value = (uint32_t)(1UL << extra) | xx_rnc_bits_read(bits, extra);
        }
        *result = value;
        return true;
    }
    return false;
}

static bool xx_rnc_unpack_method1(const uint8_t *packed, size_t packed_size,
                                  xx_rnc_output *output, xx_pd_struct *pd) {
    xx_rnc_bits bits;
    xx_rnc_huf raw_table, distance_table, length_table;
    xx_rnc_bits_init(&bits, packed, packed_size);
    xx_rnc_bits_advance(&bits, 2);
    while (output->position < output->capacity) {
        uint32_t chunk_count;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_rnc_read_huftable(&raw_table, &bits) ||
            !xx_rnc_read_huftable(&distance_table, &bits) ||
            !xx_rnc_read_huftable(&length_table, &bits)) {
            return false;
        }
        chunk_count = xx_rnc_bits_read(&bits, 16);
        for (;;) {
            uint32_t length = 0U, distance = 0U;
            if (!xx_rnc_huf_read(&raw_table, &bits, &length)) return false;
            if (length != 0U) {
                size_t start = bits.position;
                uint32_t index;
                if (length > packed_size || start > packed_size - length) {
                    return false;
                }
                for (index = 0U; index < length; ++index) {
                    xx_rnc_output_put(output, packed[start + index]);
                    if (output->failed) return false;
                }
                xx_rnc_bits_resync(&bits, start + length);
            }
            if (chunk_count == 0U || --chunk_count == 0U) break;
            if (!xx_rnc_huf_read(&distance_table, &bits, &distance) ||
                !xx_rnc_huf_read(&length_table, &bits, &length) ||
                !xx_rnc_output_copy(output, (uint64_t)distance + 1U,
                                    (uint64_t)length + 2U)) {
                return false;
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* method 2: bitwise LZ77, bits are MSB first and share the byte       */
/* stream with the literals                                            */
/* ------------------------------------------------------------------ */

typedef struct xx_rnc_stream_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t buffer;
    unsigned count;
    bool failed;
} xx_rnc_stream;

static uint8_t xx_rnc_stream_byte(xx_rnc_stream *stream) {
    if (stream->position >= stream->size) {
        stream->failed = true;
        return 0U;
    }
    return stream->data[stream->position++];
}

static uint32_t xx_rnc_stream_bits(xx_rnc_stream *stream, unsigned amount) {
    uint32_t value = 0U;
    while (amount-- != 0U) {
        if (stream->count == 0U) {
            stream->buffer = xx_rnc_stream_byte(stream);
            if (stream->failed) return 0U;
            stream->count = 8U;
        }
        value = (value << 1) | (uint32_t)((stream->buffer >> 7) & 1U);
        stream->buffer = (uint8_t)(stream->buffer << 1);
        --stream->count;
    }
    return value;
}

/* Lengths 4, 5, 6, 7, 8 and the escape value 9. */
static uint32_t xx_rnc_method2_length(xx_rnc_stream *stream) {
    uint32_t length = xx_rnc_stream_bits(stream, 1) + 4U;
    if (xx_rnc_stream_bits(stream, 1) != 0U) {
        length = ((length - 1U) << 1) + xx_rnc_stream_bits(stream, 1);
    }
    return length;
}

static uint32_t xx_rnc_method2_offset(xx_rnc_stream *stream) {
    uint32_t offset = 0U;
    if (xx_rnc_stream_bits(stream, 1) != 0U) {
        offset = xx_rnc_stream_bits(stream, 1);
        if (xx_rnc_stream_bits(stream, 1) != 0U) {
            offset = ((offset << 1) | xx_rnc_stream_bits(stream, 1)) | 4U;
            if (xx_rnc_stream_bits(stream, 1) == 0U) {
                offset = (offset << 1) | xx_rnc_stream_bits(stream, 1);
            }
        } else if (offset == 0U) {
            offset = xx_rnc_stream_bits(stream, 1) + 2U;
        }
    }
    return ((offset << 8) | xx_rnc_stream_byte(stream)) + 1U;
}

static bool xx_rnc_unpack_method2(const uint8_t *packed, size_t packed_size,
                                  xx_rnc_output *output, xx_pd_struct *pd) {
    xx_rnc_stream stream;
    stream.data = packed;
    stream.size = packed_size;
    stream.position = 0U;
    stream.buffer = 0U;
    stream.count = 0U;
    stream.failed = false;
    /* The vendor unpacker primes its bit buffer with two discarded bits. */
    (void)xx_rnc_stream_bits(&stream, 2);
    while (output->position < output->capacity) {
        bool block_done = false;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (!block_done) {
            uint32_t count, offset;
            if (stream.failed || output->failed) return false;
            if (xx_rnc_stream_bits(&stream, 1) == 0U) {
                xx_rnc_output_put(output, xx_rnc_stream_byte(&stream));
                continue;
            }
            if (xx_rnc_stream_bits(&stream, 1) != 0U) {
                if (xx_rnc_stream_bits(&stream, 1) != 0U) {
                    if (xx_rnc_stream_bits(&stream, 1) != 0U) {
                        count = xx_rnc_stream_byte(&stream);
                        if (stream.failed) return false;
                        if (count == 0U) { /* end of block marker */
                            (void)xx_rnc_stream_bits(&stream, 1);
                            block_done = true;
                            continue;
                        }
                        count += 8U;
                    } else {
                        count = 3U;
                    }
                    offset = xx_rnc_method2_offset(&stream);
                } else {
                    count = 2U;
                    offset = (uint32_t)xx_rnc_stream_byte(&stream) + 1U;
                }
            } else {
                count = xx_rnc_method2_length(&stream);
                if (count == 9U) { /* run of raw bytes */
                    uint32_t run = (xx_rnc_stream_bits(&stream, 4) << 2) + 12U;
                    uint32_t index;
                    if (stream.failed) return false;
                    for (index = 0U; index < run; ++index) {
                        xx_rnc_output_put(output,
                                          xx_rnc_stream_byte(&stream));
                        if (stream.failed || output->failed) return false;
                    }
                    continue;
                }
                offset = xx_rnc_method2_offset(&stream);
            }
            if (stream.failed ||
                !xx_rnc_output_copy(output, offset, count)) {
                return false;
            }
        }
        if (stream.failed) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* stream handling                                                     */
/* ------------------------------------------------------------------ */

static bool xx_rnc_parse_header(Abstractformat *self, xx_rnc_header *header) {
    uint8_t raw[XX_RNC_HEADER_SIZE];
    uint8_t prefix[XX_RNC_BULLFROG_SIZE];
    int64_t total_size, available, stream_at;
    if (!self || !self->device || !header || self->base_address < 0) {
        return false;
    }
    xx_mem_zero(header, sizeof(*header));
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    available = total_size - self->base_address;
    /* Bullfrog's game data files wrap an ordinary ProPack stream in an eight
     * byte "BULLFROG" tag; the stream itself is unchanged, so the tag is
     * skipped and accounted for in the format size. */
    header->prefix_size = 0;
    if (available >= (int64_t)(XX_RNC_BULLFROG_SIZE + XX_RNC_HEADER_SIZE) &&
        xx_rnc_read_exact_at(self->device, self->base_address, prefix,
                             sizeof(prefix)) &&
        xx_rt_memcmp(prefix, "BULLFROG", XX_RNC_BULLFROG_SIZE) == 0) {
        header->prefix_size = (int64_t)XX_RNC_BULLFROG_SIZE;
    }
    stream_at = self->base_address + header->prefix_size;
    available -= header->prefix_size;
    if (available < (int64_t)XX_RNC_HEADER_SIZE ||
        !xx_rnc_read_exact_at(self->device, stream_at, raw, sizeof(raw)) ||
        xx_rt_memcmp(raw, "RNC", 3U) != 0 ||
        (raw[3] != 1U && raw[3] != 2U)) {
        return false;
    }
    header->method = raw[3];
    header->unpacked_size = xx_rnc_be32(raw + 4U);
    header->packed_size = xx_rnc_be32(raw + 8U);
    header->unpacked_crc = xx_rnc_be16(raw + 12U);
    header->packed_crc = xx_rnc_be16(raw + 14U);
    header->leeway = raw[16];
    header->chunk_count = raw[17];
    header->total_size = available + header->prefix_size;
    /* Every declared size is bounded by the real file size before it is used
     * to allocate or to loop. */
    if (header->packed_size == 0U || header->unpacked_size == 0U ||
        header->packed_size >
            (uint64_t)available - (uint64_t)XX_RNC_HEADER_SIZE ||
        header->unpacked_size > XX_RNC_MAX_OUTPUT ||
        header->unpacked_size > header->packed_size * XX_RNC_MAX_RATIO ||
        header->packed_size > (uint64_t)SIZE_MAX ||
        header->unpacked_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    header->stream_size = header->prefix_size + (int64_t)XX_RNC_HEADER_SIZE +
                          (int64_t)header->packed_size;
    return true;
}

static bool xx_rnc_decode_stream(Abstractformat *self,
                                 xx_io_device *destination,
                                 xx_rnc_header *header, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    xx_rnc_output output;
    bool result = false;
    if (!xx_rnc_parse_header(self, header) || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)header->packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)header->unpacked_size);
    if (!packed || !plain ||
        !xx_rnc_read_exact_at(self->device,
                              self->base_address + header->prefix_size +
                                  (int64_t)XX_RNC_HEADER_SIZE,
                              packed, (size_t)header->packed_size) ||
        xx_rnc_crc16(packed, (size_t)header->packed_size) !=
            header->packed_crc) {
        goto cleanup;
    }
    xx_rnc_output_init(&output, plain, header->unpacked_size);
    if (header->method == 1U) {
        result = xx_rnc_unpack_method1(packed, (size_t)header->packed_size,
                                       &output, pd);
    } else {
        result = xx_rnc_unpack_method2(packed, (size_t)header->packed_size,
                                       &output, pd);
    }
    if (!result || output.failed || output.position < header->unpacked_size) {
        result = false;
        goto cleanup;
    }
    header->crc_valid = xx_rnc_crc16(plain, (size_t)header->unpacked_size) ==
                        header->unpacked_crc;
    if (destination &&
        !xx_rnc_write_all(destination, plain, (size_t)header->unpacked_size,
                          pd)) {
        result = false;
    }
cleanup:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return result;
}

static bool xx_rnc_copy_options(xx_list_s *destination,
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

static const xx_var *xx_rnc_find_option(const xx_list_s *options,
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

static bool xx_rnc_populate_record(Abstractformat *self,
                                   xx_archive_record *record) {
    const xx_rnc *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= 0) {
        return false;
    }
    archive = (const xx_rnc *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* stream_end walks back to where the 18 byte header really begins, which
     * is past the optional "BULLFROG" tag. */
    record->header_offset = archive->stream_end -
                            (int64_t)XX_RNC_HEADER_SIZE -
                            (int64_t)archive->packed_size;
    record->header_size = (int64_t)XX_RNC_HEADER_SIZE;
    record->data_offset = archive->stream_end -
                          (int64_t)archive->packed_size;
    record->compressed_size = (int64_t)archive->packed_size;
    /* The stream stores no name; CRC32 carries the format's CRC16 value. */
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_RNC_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          archive->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          archive->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          archive->unpacked_crc) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          archive->leeway) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           self->is_crypted);
}

/* ------------------------------------------------------------------ */
/* format interface                                                    */
/* ------------------------------------------------------------------ */

void xx_rnc_init(xx_rnc *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_RNC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rnc");
    xx_format_set_extension(&archive->format, "rnc");
    archive->format.check_is_valid = xx_rnc_check_is_valid;
    archive->format.handle_base_info = xx_rnc_handle_base_info;
    archive->format.get_format_size = xx_rnc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rnc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rnc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rnc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rnc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rnc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rnc_free_archive_records_reading;
    archive->format.destroy = xx_rnc_vtable_destroy;
    archive->stream_end = -1;
}

xx_rnc *xx_rnc_create(xx_io_device *device, int64_t base_address) {
    xx_rnc *archive = (xx_rnc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rnc_init(archive, device, base_address);
    return archive;
}

void xx_rnc_destroy(xx_rnc *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->unpacked_size = 0U;
    archive->packed_size = 0U;
    archive->stream_end = -1;
    archive->method = 0U;
}

static void xx_rnc_vtable_destroy(Abstractformat *self) {
    xx_rnc_destroy((xx_rnc *)self);
}

void xx_rnc_free(xx_rnc *archive) {
    if (!archive) return;
    xx_rnc_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rnc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rnc_header header;
    return xx_rnc_decode_stream(self, NULL, &header, pd);
}

bool xx_rnc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rnc_header header;
    xx_rnc *archive;
    if (!self || !xx_rnc_decode_stream(self, NULL, &header, pd)) {
        if (self) {
            archive = (xx_rnc *)self;
            archive->unpacked_size = 0U;
            archive->packed_size = 0U;
            archive->stream_end = -1;
            archive->method = 0U;
            archive->leeway = 0U;
            archive->chunk_count = 0U;
            archive->unpacked_crc = 0U;
            archive->packed_crc = 0U;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_rnc *)self;
    archive->unpacked_size = header.unpacked_size;
    archive->packed_size = header.packed_size;
    archive->stream_end = self->base_address + header.stream_size;
    archive->method = header.method;
    archive->leeway = header.leeway;
    archive->chunk_count = header.chunk_count;
    archive->unpacked_crc = header.unpacked_crc;
    archive->packed_crc = header.packed_crc;
    self->format_size = header.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = header.total_size > header.stream_size
                               ? self->base_address + header.stream_size
                               : -1;
    self->overlay_size = header.total_size > header.stream_size
                             ? header.total_size - header.stream_size
                             : 0;
    self->file_type = XX_RNC_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    /* A stream packed with the vendor "K" option decodes into key-XORed
     * bytes, which is exactly the case the declared CRC16 fails to match. */
    self->is_crypted = !header.crc_valid;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_rnc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_rnc_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_rnc_unpack_to_device(xx_rnc *archive, xx_io_device *destination,
                             xx_pd_struct *pd) {
    xx_rnc_header header;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !xx_rnc_decode_stream(&archive->format, destination, &header, pd)) {
        return false;
    }
    return header.stream_size == archive->format.format_size &&
           header.unpacked_size == archive->unpacked_size;
}

xx_archive_record_state *xx_rnc_create_archive_records_reading(
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
    if (!xx_rnc_copy_options(&state->options, options) ||
        !xx_rnc_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_rnc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rnc_archive_record_move_to_next(Abstractformat *self,
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

bool xx_rnc_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path = NULL;
    bool result;
    xx_rnc *archive = (xx_rnc *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = xx_rnc_find_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        xx_rnc_header header;
        return xx_rnc_decode_stream(self, NULL, &header, pd) &&
               header.stream_size == self->format_size &&
               header.unpacked_size == archive->unpacked_size;
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
                                          XX_RNC_PAYLOAD_NAME);
    } else {
        destination_path = xx_str_concat(base_path, XX_RNC_PAYLOAD_NAME);
    }
    xx_str_free(owned_path);
    if (!destination_path ||
        !xx_store_create_dirs_a(destination_path, false)) {
        xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_rnc_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_rnc_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_rnc_get_unpacked_size(const xx_rnc *archive) {
    return archive ? archive->unpacked_size : 0U;
}

int64_t xx_rnc_get_stream_end(const xx_rnc *archive) {
    return archive ? archive->stream_end : -1;
}

uint8_t xx_rnc_get_method(const xx_rnc *archive) {
    return archive ? archive->method : 0U;
}
