/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Decrunchers for the crunched data files found on Amiga disks without a
 * filesystem: PowerPacker (PP20), Rob Northen ProPack (RNC 1 and 2),
 * Crunch-Mania (CrM!, CrM2, Crm!, Crm2), File Imploder (IMP! and the ids
 * of its clones), Pack-Ice (Ice!, ICE! and the ids of its clones) and
 * StoneCracker (S300, S401, S403, S404).  The Crunch-Mania, Imploder,
 * Pack-Ice and StoneCracker decoders are bounded native C adaptations of
 * Ancient's CRMDecompressor.cpp, IMPDecompressor.cpp, IceDecompressor.cpp
 * and StoneCrackerDecompressor.cpp; see LICENSE.ancient.
 *
 * Everything here works on a memory image.  A file is accepted only when
 * its whole stream decodes: ProPack by both of its CRCs, Imploder by its
 * checksum, the others by a decode that ends exactly at the first byte of
 * the stream (StoneCracker 4.01 reads forwards and pads its stream: at
 * most four bytes may be left).  PowerPacker stores no crunched length, so
 * its end is searched: every longword-aligned position whose trailer is
 * plausible is tried, and the first one that decodes with at most one bit
 * to spare is taken (measured: genuine files leave 0 or 1 bit, never more).
 */
#ifndef XX_ADF_CRUNCH_H
#define XX_ADF_CRUNCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define ADFC_NONE 0U
#define ADFC_PP20 1U
#define ADFC_RNC 2U
#define ADFC_CRM 3U
#define ADFC_IMP 4U
#define ADFC_ICE 5U
#define ADFC_SC 6U

/** No Amiga floppy file decrunches to more than this. */
#define ADFC_MAX_RAW UINT32_C(0x1000000)
/** Bytes all attempts on one image may read (CRC, checksum, stream) and
 * produce; no real disk comes near it. */
#define ADFC_MAX_WORK (UINT64_C(256) * 1024U * 1024U)
/** PowerPacker trailer positions one image may have examined. */
#define ADFC_MAX_TESTS UINT64_C(0x4000000)

/* PowerPacker cannot beat 3 bits for 7 bytes, about 19:1. */
#define ADFC_PP_RATIO 24U
#define ADFC_PP_SLACK 64U
#define ADFC_PP_HEADER 8U
#define ADFC_PP_MAX_SKIP 31U
/* A stream that begins with a match carries one bit that is never read. */
#define ADFC_PP_MAX_LEFT 1U
#define ADFC_RNC_HEADER 18U
#define ADFC_RNC_MAX_RATIO 8192U
#define ADFC_RNC_OVERSHOOT 0x10000U
#define ADFC_CRM_HEADER 14U
#define ADFC_CRM_NODES 8193U
#define ADFC_IMP_TAIL 0x32U
#define ADFC_IMP_SUM 0x2EU

typedef struct adfc_state_s {
    uint64_t work;  /**< Bytes produced by decode attempts so far. */
    uint64_t tests; /**< PowerPacker trailer positions examined so far. */
    uint8_t *scratch;
    size_t scratch_size;
} adfc_state;

static uint32_t adfc_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static uint32_t adfc_be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8U) | (uint32_t)p[1];
}

/* ---------------------------------------------------------------------- */
/* PowerPacker                                                             */

typedef struct adfc_pp_bits_s {
    const uint8_t *data;
    size_t position; /**< Next byte to load is data[position - 1]. */
    size_t floor;
    uint32_t buffer;
    unsigned count;
    bool error;
} adfc_pp_bits;

/* Bits leave each byte at its low end and enter the result at its high end;
 * the stream is walked from its last byte to its first. */
static uint32_t adfc_pp_get(adfc_pp_bits *bits, unsigned width)
{
    uint32_t result = 0U;
    unsigned index;
    if (bits->error) return 0U;
    for (index = 0U; index < width; ++index) {
        if (bits->count == 0U) {
            if (bits->position <= bits->floor) {
                bits->error = true;
                return 0U;
            }
            bits->buffer = bits->data[--bits->position];
            bits->count = 8U;
        }
        result = (result << 1U) | (bits->buffer & 1U);
        bits->buffer >>= 1U;
        --bits->count;
    }
    return result;
}

static bool adfc_pp_widths_ok(const uint8_t *widths)
{
    uint32_t mode = adfc_be32(widths);
    return mode == UINT32_C(0x09090909) || mode == UINT32_C(0x090A0A0A) || mode == UINT32_C(0x090A0B0B) || mode == UINT32_C(0x090A0C0C) || mode == UINT32_C(0x090A0C0D);
}

/* Decode the stream data[start..end) into out[0..size).  @p made receives
 * the bytes produced, also when the stream is bad; @p left the stream bits
 * that were not read. */
static bool adfc_pp_decode(const uint8_t *data, size_t start, size_t end, const uint8_t *widths, unsigned skip, uint8_t *out, size_t size, size_t *made, uint64_t *left)
{
    adfc_pp_bits bits;
    size_t cursor = size;
    bool good = false;
    bits.data = data;
    bits.position = end;
    bits.floor = start;
    bits.buffer = 0U;
    bits.count = 0U;
    bits.error = false;
    (void)adfc_pp_get(&bits, skip);
    while (!bits.error) {
        uint32_t index, offset, code, length;
        unsigned width;
        if (cursor == 0U) {
            good = true;
            break;
        }
        if (adfc_pp_get(&bits, 1U) == 0U) {
            uint32_t run = 0U;
            do {
                code = adfc_pp_get(&bits, 2U);
                run += code;
            } while (code == 3U && !bits.error && run <= ADFC_MAX_RAW);
            length = run + 1U;
            if (bits.error || length > cursor) break;
            while (length-- != 0U) out[--cursor] = (uint8_t)adfc_pp_get(&bits, 8U);
            if (bits.error) break;
            if (cursor == 0U) {
                good = true;
                break;
            }
        }
        index = adfc_pp_get(&bits, 2U);
        width = widths[index];
        length = index + 2U;
        if (index == 3U) {
            if (adfc_pp_get(&bits, 1U) == 0U) width = 7U;
            offset = adfc_pp_get(&bits, width);
            do {
                code = adfc_pp_get(&bits, 3U);
                length += code;
            } while (code == 7U && !bits.error && length <= ADFC_MAX_RAW);
        } else {
            offset = adfc_pp_get(&bits, width);
        }
        /* The source of a match lies ahead of the write cursor, among the
         * bytes already produced. */
        if (bits.error || length > cursor || (size_t)offset >= size - cursor) break;
        while (length-- != 0U) {
            out[cursor - 1U] = out[cursor + offset];
            --cursor;
        }
    }
    if (made) *made = size - cursor;
    if (left) *left = (uint64_t)(bits.position - bits.floor) * 8U + bits.count;
    return good;
}

/* Find the end of the PowerPacker file at data[0] and decode it. */
static uint32_t adfc_pp_open(const uint8_t *data, size_t limit, uint8_t **plain, uint32_t *raw_size, adfc_state *state)
{
    size_t count, index;
    if (limit < ADFC_PP_HEADER + 8U || !adfc_pp_widths_ok(data + 4U)) return 0U;
    /* A file ends before the next PowerPacker header: without this a file
     * with a damaged end is searched through every file behind it. */
    for (index = ADFC_PP_HEADER; index + 8U <= limit; ++index) {
        if (data[index] == 'P' && data[index + 1U] == 'P' && data[index + 2U] == '2' && data[index + 3U] == '0' && adfc_pp_widths_ok(data + index + 4U)) {
            limit = index;
            break;
        }
    }
    count = (limit - ADFC_PP_HEADER) / 4U;
    for (index = 1U; index < count; ++index) {
        const uint8_t *trailer = data + ADFC_PP_HEADER + 4U * index;
        uint32_t size = ((uint32_t)trailer[0] << 16U) | ((uint32_t)trailer[1] << 8U) | (uint32_t)trailer[2];
        size_t made = 0U;
        uint64_t left = 1U;
        bool good;
        if (++state->tests > ADFC_MAX_TESTS) return 0U;
        if (trailer[3] > ADFC_PP_MAX_SKIP || size == 0U || (uint64_t)size > (uint64_t)index * 4U * ADFC_PP_RATIO + ADFC_PP_SLACK) continue;
        if (state->work > ADFC_MAX_WORK) return 0U;
        if (state->scratch_size < size) {
            size_t wanted = (size_t)ADFC_MAX_RAW;
            if ((uint64_t)count * 4U * ADFC_PP_RATIO + ADFC_PP_SLACK < wanted) wanted = count * 4U * ADFC_PP_RATIO + ADFC_PP_SLACK;
            if (wanted < size) wanted = size;
            if (state->scratch) xx_mem_free(state->scratch);
            state->scratch = (uint8_t *)xx_mem_alloc(wanted);
            state->scratch_size = state->scratch ? wanted : 0U;
            if (!state->scratch) return 0U;
        }
        good = adfc_pp_decode(data, ADFC_PP_HEADER, ADFC_PP_HEADER + 4U * index, data + 4U, trailer[3], state->scratch, size, &made, &left);
        state->work += made + 16U;
        if (!good || left > ADFC_PP_MAX_LEFT) continue;
        *plain = (uint8_t *)xx_mem_alloc(size);
        if (!*plain) return 0U;
        xx_rt_memcpy(*plain, state->scratch, size);
        *raw_size = size;
        return (uint32_t)(ADFC_PP_HEADER + 4U * index + 4U);
    }
    return 0U;
}

/* ---------------------------------------------------------------------- */
/* Rob Northen ProPack                                                     */

static uint32_t adfc_crc16(const uint8_t *data, size_t size)
{
    static const uint16_t nibble[16] = {0x0000U, 0xCC01U, 0xD801U, 0x1400U, 0xF001U, 0x3C00U, 0x2800U, 0xE401U,
                                        0xA001U, 0x6C00U, 0x7800U, 0xB401U, 0x5000U, 0x9C01U, 0x8801U, 0x4400U};
    uint32_t crc = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        crc ^= data[index];
        crc = (crc >> 4U) ^ nibble[crc & 0x0FU];
        crc = (crc >> 4U) ^ nibble[crc & 0x0FU];
    }
    return crc & 0xFFFFU;
}

/* Bytes past the declared size are counted but dropped: the vendor
 * unpackers rely on a "leeway" area the caller never sees. */
typedef struct adfc_rnc_out_s {
    uint8_t *data;
    uint64_t capacity;
    uint64_t position;
    bool failed;
} adfc_rnc_out;

static void adfc_rnc_put(adfc_rnc_out *out, uint8_t value)
{
    if (out->position < out->capacity) {
        out->data[out->position] = value;
    } else if (out->position >= out->capacity + ADFC_RNC_OVERSHOOT) {
        out->failed = true;
        return;
    }
    ++out->position;
}

static bool adfc_rnc_copy(adfc_rnc_out *out, uint32_t distance, uint32_t length)
{
    uint32_t index;
    if (distance == 0U || distance > out->position) return false;
    for (index = 0U; index < length; ++index) {
        uint64_t from = out->position - distance;
        adfc_rnc_put(out, from < out->capacity ? out->data[from] : 0U);
        if (out->failed) return false;
    }
    return true;
}

/* Method 1 keeps 16..31 bits taken from little-endian 16-bit words;
 * `position` is the offset of the word loaded last, which is also where a
 * run of raw bytes starts. */
typedef struct adfc_rnc_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t buffer;
    unsigned count;
} adfc_rnc_bits;

static uint32_t adfc_rnc_word(const adfc_rnc_bits *bits, size_t position)
{
    uint32_t value = 0U;
    if (position < bits->size) value = bits->data[position];
    if (position < bits->size && position + 1U < bits->size) value |= (uint32_t)bits->data[position + 1U] << 8U;
    return value;
}

static void adfc_rnc_advance(adfc_rnc_bits *bits, unsigned amount)
{
    bits->buffer >>= amount;
    bits->count -= amount;
    if (bits->count < 16U) {
        if (bits->position <= bits->size) bits->position += 2U;
        bits->buffer |= adfc_rnc_word(bits, bits->position) << bits->count;
        bits->count += 16U;
    }
}

static uint32_t adfc_rnc_read(adfc_rnc_bits *bits, unsigned amount)
{
    uint32_t value = amount == 0U ? 0U : bits->buffer & ((UINT32_C(1) << amount) - 1U);
    adfc_rnc_advance(bits, amount);
    return value;
}

typedef struct adfc_rnc_huf_s {
    unsigned count;
    uint32_t code[32];
    uint8_t length[32];
    uint8_t value[32];
} adfc_rnc_huf;

static void adfc_rnc_table(adfc_rnc_huf *table, adfc_rnc_bits *bits)
{
    unsigned lengths[32];
    unsigned number, index, level, longest = 1U;
    uint32_t code = 0U;
    table->count = 0U;
    number = (unsigned)adfc_rnc_read(bits, 5U);
    for (index = 0U; index < number; ++index) {
        lengths[index] = (unsigned)adfc_rnc_read(bits, 4U);
        if (lengths[index] > longest) longest = lengths[index];
    }
    for (level = 1U; level <= longest && number != 0U; ++level) {
        for (index = 0U; index < number; ++index) {
            uint32_t mirrored = 0U, value = code;
            unsigned bit;
            if (lengths[index] != level) continue;
            for (bit = 0U; bit < level; ++bit) {
                mirrored = (mirrored << 1U) | (value & 1U);
                value >>= 1U;
            }
            table->code[table->count] = mirrored;
            table->length[table->count] = (uint8_t)level;
            table->value[table->count] = (uint8_t)index;
            ++table->count;
            ++code;
        }
        code <<= 1U;
    }
}

static bool adfc_rnc_symbol(const adfc_rnc_huf *table, adfc_rnc_bits *bits, uint32_t *result)
{
    unsigned index;
    for (index = 0U; index < table->count; ++index) {
        uint32_t value;
        if ((bits->buffer & ((UINT32_C(1) << table->length[index]) - 1U)) != table->code[index]) continue;
        adfc_rnc_advance(bits, table->length[index]);
        value = table->value[index];
        if (value >= 2U) {
            unsigned extra = value - 1U;
            if (extra > 16U) return false;
            value = (UINT32_C(1) << extra) | adfc_rnc_read(bits, extra);
        }
        *result = value;
        return true;
    }
    return false;
}

static bool adfc_rnc_method1(const uint8_t *packed, size_t size, adfc_rnc_out *out)
{
    adfc_rnc_bits bits;
    adfc_rnc_huf raw_table, distance_table, length_table;
    bits.data = packed;
    bits.size = size;
    bits.position = 0U;
    bits.buffer = adfc_rnc_word(&bits, 0U);
    bits.count = 16U;
    adfc_rnc_advance(&bits, 2U);
    while (out->position < out->capacity) {
        uint32_t chunks;
        adfc_rnc_table(&raw_table, &bits);
        adfc_rnc_table(&distance_table, &bits);
        adfc_rnc_table(&length_table, &bits);
        chunks = adfc_rnc_read(&bits, 16U);
        for (;;) {
            uint32_t length = 0U, distance = 0U, index;
            if (!adfc_rnc_symbol(&raw_table, &bits, &length)) return false;
            if (length != 0U) {
                size_t start = bits.position;
                if (length > size || start > size - length) return false;
                for (index = 0U; index < length; ++index) {
                    adfc_rnc_put(out, packed[start + index]);
                    if (out->failed) return false;
                }
                /* The prefetched word was the start of the run: drop it and
                 * load the word behind the run. */
                bits.count -= 16U;
                bits.buffer &= (UINT32_C(1) << bits.count) - 1U;
                bits.position = start + length;
                bits.buffer |= adfc_rnc_word(&bits, bits.position) << bits.count;
                bits.count += 16U;
            }
            if (chunks == 0U || --chunks == 0U) break;
            if (!adfc_rnc_symbol(&distance_table, &bits, &distance) || !adfc_rnc_symbol(&length_table, &bits, &length) || !adfc_rnc_copy(out, distance + 1U, length + 2U))
                return false;
        }
    }
    return true;
}

/* Method 2: bits are taken MSB first and share the byte stream with the
 * literals. */
typedef struct adfc_rnc_stream_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t buffer;
    unsigned count;
    bool failed;
} adfc_rnc_stream;

static uint8_t adfc_rnc_byte(adfc_rnc_stream *stream)
{
    if (stream->position >= stream->size) {
        stream->failed = true;
        return 0U;
    }
    return stream->data[stream->position++];
}

static uint32_t adfc_rnc_bits2(adfc_rnc_stream *stream, unsigned amount)
{
    uint32_t value = 0U;
    while (amount-- != 0U) {
        if (stream->count == 0U) {
            stream->buffer = adfc_rnc_byte(stream);
            if (stream->failed) return 0U;
            stream->count = 8U;
        }
        value = (value << 1U) | (uint32_t)((stream->buffer >> 7U) & 1U);
        stream->buffer = (uint8_t)(stream->buffer << 1U);
        --stream->count;
    }
    return value;
}

static uint32_t adfc_rnc_offset2(adfc_rnc_stream *stream)
{
    uint32_t offset = 0U;
    if (adfc_rnc_bits2(stream, 1U) != 0U) {
        offset = adfc_rnc_bits2(stream, 1U);
        if (adfc_rnc_bits2(stream, 1U) != 0U) {
            offset = ((offset << 1U) | adfc_rnc_bits2(stream, 1U)) | 4U;
            if (adfc_rnc_bits2(stream, 1U) == 0U) offset = (offset << 1U) | adfc_rnc_bits2(stream, 1U);
        } else if (offset == 0U) {
            offset = adfc_rnc_bits2(stream, 1U) + 2U;
        }
    }
    return ((offset << 8U) | adfc_rnc_byte(stream)) + 1U;
}

static bool adfc_rnc_method2(const uint8_t *packed, size_t size, adfc_rnc_out *out)
{
    adfc_rnc_stream stream;
    stream.data = packed;
    stream.size = size;
    stream.position = 0U;
    stream.buffer = 0U;
    stream.count = 0U;
    stream.failed = false;
    (void)adfc_rnc_bits2(&stream, 2U);
    while (out->position < out->capacity) {
        bool block_done = false;
        while (!block_done) {
            uint32_t count, offset;
            if (stream.failed || out->failed) return false;
            if (adfc_rnc_bits2(&stream, 1U) == 0U) {
                adfc_rnc_put(out, adfc_rnc_byte(&stream));
                continue;
            }
            if (adfc_rnc_bits2(&stream, 1U) != 0U) {
                if (adfc_rnc_bits2(&stream, 1U) != 0U) {
                    if (adfc_rnc_bits2(&stream, 1U) != 0U) {
                        count = adfc_rnc_byte(&stream);
                        if (stream.failed) return false;
                        if (count == 0U) { /* end of block */
                            (void)adfc_rnc_bits2(&stream, 1U);
                            block_done = true;
                            continue;
                        }
                        count += 8U;
                    } else {
                        count = 3U;
                    }
                    offset = adfc_rnc_offset2(&stream);
                } else {
                    count = 2U;
                    offset = (uint32_t)adfc_rnc_byte(&stream) + 1U;
                }
            } else {
                count = adfc_rnc_bits2(&stream, 1U) + 4U;
                if (adfc_rnc_bits2(&stream, 1U) != 0U) count = ((count - 1U) << 1U) + adfc_rnc_bits2(&stream, 1U);
                if (count == 9U) { /* run of raw bytes */
                    uint32_t run = (adfc_rnc_bits2(&stream, 4U) << 2U) + 12U, index;
                    if (stream.failed) return false;
                    for (index = 0U; index < run; ++index) {
                        adfc_rnc_put(out, adfc_rnc_byte(&stream));
                        if (stream.failed || out->failed) return false;
                    }
                    continue;
                }
                offset = adfc_rnc_offset2(&stream);
            }
            if (stream.failed || !adfc_rnc_copy(out, offset, count)) return false;
        }
        if (stream.failed) return false;
    }
    return true;
}

/* Sizes from the header at data[0]; false when they cannot be a file. */
static bool adfc_rnc_sizes(const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *packed_size)
{
    if (limit < ADFC_RNC_HEADER) return false;
    *raw_size = adfc_be32(data + 4U);
    *packed_size = adfc_be32(data + 8U);
    return *packed_size != 0U && *raw_size != 0U && *packed_size <= limit - ADFC_RNC_HEADER && *raw_size <= ADFC_MAX_RAW &&
           (uint64_t)*raw_size <= (uint64_t)*packed_size * ADFC_RNC_MAX_RATIO;
}

/* Decode the body of a ProPack file whose sizes and packed CRC are good. */
static bool adfc_rnc_decode(const uint8_t *data, uint32_t packed_size, uint8_t *out, uint32_t raw_size, uint64_t *made)
{
    adfc_rnc_out output;
    bool good;
    output.data = out;
    output.capacity = raw_size;
    output.position = 0U;
    output.failed = false;
    good = data[3] == 1U ? adfc_rnc_method1(data + ADFC_RNC_HEADER, packed_size, &output) : adfc_rnc_method2(data + ADFC_RNC_HEADER, packed_size, &output);
    if (made) *made = output.position < raw_size ? output.position : raw_size;
    return good && !output.failed && output.position >= raw_size && adfc_crc16(out, raw_size) == adfc_be16(data + 12U);
}

/* ---------------------------------------------------------------------- */
/* Crunch-Mania                                                            */

typedef struct adfc_crm_node_s {
    int32_t child[2], symbol;
} adfc_crm_node;

typedef struct adfc_crm_tree_s {
    adfc_crm_node node[ADFC_CRM_NODES];
    uint32_t count;
} adfc_crm_tree;

/* The stream is read from its last byte to its first, low bit first. */
typedef struct adfc_crm_bits_s {
    const uint8_t *data;
    size_t at;
    size_t floor;
    uint32_t word;
    unsigned left;
} adfc_crm_bits;

static bool adfc_crm_read(adfc_crm_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        if (!bits->left) {
            if (bits->at <= bits->floor) return false;
            bits->word = bits->data[--bits->at];
            bits->left = 8U;
        }
        result |= (bits->word & 1U) << index;
        bits->word >>= 1U;
        --bits->left;
    }
    *value = result;
    return true;
}

static bool adfc_crm_insert(adfc_crm_tree *tree, unsigned width, uint32_t code, uint32_t symbol)
{
    uint32_t at = 0U;
    unsigned index;
    for (index = 0U; index < width; ++index) {
        uint32_t bit = (code >> (width - index - 1U)) & 1U;
        int32_t next;
        if (tree->node[at].symbol >= 0) return false;
        next = tree->node[at].child[bit];
        if (next < 0) {
            if (tree->count >= ADFC_CRM_NODES) return false;
            next = (int32_t)tree->count++;
            tree->node[at].child[bit] = next;
            tree->node[next].child[0] = -1;
            tree->node[next].child[1] = -1;
            tree->node[next].symbol = -1;
        }
        at = (uint32_t)next;
    }
    if (tree->node[at].symbol >= 0 || tree->node[at].child[0] >= 0 || tree->node[at].child[1] >= 0) return false;
    tree->node[at].symbol = (int32_t)symbol;
    return true;
}

static bool adfc_crm_table(adfc_crm_tree *tree, adfc_crm_bits *bits, unsigned symbol_bits)
{
    uint32_t max_depth, counts[15], code = 0U;
    unsigned depth, index;
    tree->count = 1U;
    tree->node[0].child[0] = -1;
    tree->node[0].child[1] = -1;
    tree->node[0].symbol = -1;
    if (!adfc_crm_read(bits, 4U, &max_depth) || !max_depth) return false;
    for (depth = 1U; depth <= max_depth; ++depth) {
        if (!adfc_crm_read(bits, depth < symbol_bits ? depth : symbol_bits, &counts[depth - 1U])) return false;
    }
    for (depth = 1U; depth <= max_depth; ++depth) {
        uint32_t step = UINT32_C(1) << (max_depth - depth);
        for (index = 0U; index < counts[depth - 1U]; ++index) {
            uint32_t symbol;
            if (code >= (UINT32_C(1) << max_depth) || !adfc_crm_read(bits, symbol_bits, &symbol) || !adfc_crm_insert(tree, depth, code >> (max_depth - depth), symbol))
                return false;
            code += step;
        }
    }
    return tree->count > 1U;
}

static bool adfc_crm_symbol(const adfc_crm_tree *tree, adfc_crm_bits *bits, uint32_t *value)
{
    uint32_t at = 0U;
    unsigned depth = 0U;
    while (tree->node[at].symbol < 0) {
        uint32_t bit;
        int32_t next;
        if (depth++ >= 15U || !adfc_crm_read(bits, 1U, &bit)) return false;
        next = tree->node[at].child[bit];
        if (next < 0) return false;
        at = (uint32_t)next;
    }
    *value = (uint32_t)tree->node[at].symbol;
    return true;
}

static bool adfc_crm_copy(uint8_t *out, size_t size, size_t *pos, uint32_t distance, uint32_t count)
{
    if (!distance || count > *pos || distance > size - *pos) return false;
    while (count-- != 0U) {
        --*pos;
        out[*pos] = out[*pos + distance];
    }
    return true;
}

static bool adfc_crm_lzh(adfc_crm_bits *bits, adfc_crm_tree *trees, uint8_t *out, size_t size, size_t *pos)
{
    size_t rounds = 0U;
    for (;;) {
        uint32_t items, index, again;
        if (++rounds > size + 1U || !adfc_crm_table(&trees[0], bits, 9U) || !adfc_crm_table(&trees[1], bits, 4U) || !adfc_crm_read(bits, 16U, &items)) return false;
        ++items;
        for (index = 0U; index < items; ++index) {
            uint32_t count, width, distance;
            if (!adfc_crm_symbol(&trees[0], bits, &count)) return false;
            if (count & 0x100U) {
                if (!*pos) return false;
                out[--*pos] = (uint8_t)count;
                continue;
            }
            count += 3U;
            if (!adfc_crm_symbol(&trees[1], bits, &width) || width > 15U) return false;
            if (!width) {
                if (!adfc_crm_read(bits, 1U, &distance)) return false;
                ++distance;
            } else {
                if (!adfc_crm_read(bits, width, &distance)) return false;
                distance = (distance | (UINT32_C(1) << width)) + 1U;
            }
            if (!adfc_crm_copy(out, size, pos, distance, count)) return false;
        }
        if (!adfc_crm_read(bits, 1U, &again)) return false;
        if (!again) return *pos == 0U;
    }
}

static bool adfc_crm_standard(adfc_crm_bits *bits, uint8_t *out, size_t size, size_t *pos)
{
    static const uint8_t length_bits[4] = {1U, 2U, 4U, 8U};
    static const uint8_t length_base[4] = {2U, 4U, 8U, 24U};
    static const uint8_t distance_bits[3] = {5U, 9U, 14U};
    static const uint16_t distance_base[3] = {0U, 32U, 544U};
    while (*pos) {
        uint32_t bit, count, distance, value;
        unsigned index;
        if (!adfc_crm_read(bits, 1U, &bit)) return false;
        if (bit) {
            if (!adfc_crm_read(bits, 8U, &value)) return false;
            out[--*pos] = (uint8_t)value;
            continue;
        }
        for (index = 0U; index < 3U; ++index) { /* 0, 10, 110, 111 */
            if (!adfc_crm_read(bits, 1U, &bit)) return false;
            if (!bit) break;
        }
        if (!adfc_crm_read(bits, length_bits[index], &count)) return false;
        count += length_base[index];
        if (count == 23U) { /* a run of literals */
            if (!adfc_crm_read(bits, 1U, &bit) || !adfc_crm_read(bits, bit ? 5U : 14U, &count)) return false;
            count += 15U;
            if (count > *pos) return false;
            while (count-- != 0U) {
                if (!adfc_crm_read(bits, 8U, &value)) return false;
                out[--*pos] = (uint8_t)value;
            }
            continue;
        }
        if (count > 23U) --count;
        if (!adfc_crm_read(bits, 1U, &bit)) return false; /* 0 -> 1, 10 -> 0, 11 -> 2 */
        index = 1U;
        if (bit) {
            if (!adfc_crm_read(bits, 1U, &bit)) return false;
            index = bit ? 2U : 0U;
        }
        if (!adfc_crm_read(bits, distance_bits[index], &distance)) return false;
        if (!adfc_crm_copy(out, size, pos, distance + distance_base[index], count)) return false;
    }
    return true;
}

static bool adfc_crm_sizes(const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *packed_size)
{
    if (limit < 20U) return false;
    *raw_size = adfc_be32(data + 6U);
    *packed_size = adfc_be32(data + 10U);
    return *raw_size != 0U && *raw_size <= ADFC_MAX_RAW && *packed_size >= 6U && *packed_size <= limit - ADFC_CRM_HEADER &&
           adfc_be16(data + ADFC_CRM_HEADER + *packed_size - 2U) <= 16U;
}

static bool adfc_crm_decode(const uint8_t *data, uint32_t packed_size, uint8_t *out, uint32_t raw_size, uint64_t *made)
{
    adfc_crm_bits bits;
    adfc_crm_tree *trees = NULL;
    size_t pos = raw_size, at = (size_t)ADFC_CRM_HEADER + packed_size - 6U, index;
    uint32_t shift = adfc_be16(data + at + 4U);
    bool lzh = data[3] == '2', good;
    uint8_t sum = 0U;
    bits.data = data;
    bits.at = at;
    bits.floor = ADFC_CRM_HEADER;
    bits.word = adfc_be32(data + at) >> (16U - shift);
    bits.left = shift + 16U;
    if (lzh) {
        trees = (adfc_crm_tree *)xx_mem_alloc(2U * sizeof(*trees));
        if (!trees) return false;
    }
    good = lzh ? adfc_crm_lzh(&bits, trees, out, raw_size, &pos) : adfc_crm_standard(&bits, out, raw_size, &pos);
    if (trees) xx_mem_free(trees);
    if (made) *made = raw_size - pos;
    /* A genuine stream is used up to its first byte. */
    if (!good || pos != 0U || bits.at != bits.floor) return false;
    if (data[2] == 'm') { /* sample data, stored as deltas */
        for (index = 0U; index < raw_size; ++index) {
            sum = (uint8_t)(sum + out[index]);
            out[index] = sum;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* File Imploder                                                           */

typedef struct adfc_imp_bits_s {
    const uint8_t *data;
    size_t size, at, reference;
    uint8_t word;
    unsigned left;
} adfc_imp_bits;

/* The first twelve bytes of the stream were moved behind it to make room
 * for the header. */
static bool adfc_imp_byte(adfc_imp_bits *bits, uint8_t *value)
{
    size_t index;
    if (!bits->at) return false;
    index = --bits->at;
    if (index < 12U) {
        if (index < 4U) index += bits->reference + 8U;
        else if (index < 8U) index += bits->reference;
        else index += bits->reference - 8U;
    }
    if (index >= bits->size) return false;
    *value = bits->data[index];
    return true;
}

static bool adfc_imp_read(adfc_imp_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        if (!bits->left) {
            if (!adfc_imp_byte(bits, &bits->word)) return false;
            bits->left = 8U;
        }
        --bits->left;
        result = (result << 1U) | ((uint32_t)(bits->word >> bits->left) & 1U);
    }
    *value = result;
    return true;
}

/* 0 -> 0, 10 -> 1, 11 -> 2 */
static bool adfc_imp_three(adfc_imp_bits *bits, uint32_t *symbol)
{
    uint32_t bit;
    if (!adfc_imp_read(bits, 1U, &bit)) return false;
    *symbol = 0U;
    if (!bit) return true;
    if (!adfc_imp_read(bits, 1U, &bit)) return false;
    *symbol = bit ? 2U : 1U;
    return true;
}

/* The checksum addition of an Imploder id, 0 when @p tag is not one. */
static uint32_t adfc_imp_addition(const uint8_t *tag)
{
    uint32_t id = adfc_be32(tag);
    if (id == UINT32_C(0x494D5021) /* IMP! */ || id == UINT32_C(0x41544E21) /* ATN! */ || id == UINT32_C(0x4544414D) /* EDAM */ || id == UINT32_C(0x4D2E482E) /* M.H. */)
        return 7U;
    if (id == UINT32_C(0x42445049) /* BDPI */) return 0x6E8U;
    if (id == UINT32_C(0x43484649) /* CHFI */) return 0xFE4U;
    return 0U;
}

/* Sizes from the header at data[0]; false when they cannot be a file. */
static bool adfc_imp_sizes(const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *packed_size)
{
    uint32_t end;
    if (limit < ADFC_IMP_TAIL || !adfc_imp_addition(data)) return false;
    *raw_size = adfc_be32(data + 4U);
    end = adfc_be32(data + 8U);
    if ((end & 1U) || end < 12U || end > limit - ADFC_IMP_TAIL || *raw_size == 0U || *raw_size > ADFC_MAX_RAW) return false;
    *packed_size = end + ADFC_IMP_TAIL;
    return true;
}

/* The checksum of an Imploder file of @p packed_size bytes. */
static bool adfc_imp_sum_ok(const uint8_t *data, uint32_t packed_size)
{
    uint32_t sum = adfc_imp_addition(data), index;
    for (index = 0U; index < packed_size - 4U; index += 2U) sum += adfc_be16(data + index);
    return sum == adfc_be32(data + packed_size - 4U);
}

static bool adfc_imp_decode(const uint8_t *data, uint32_t packed_size, uint8_t *out, uint32_t raw_size, uint64_t *made)
{
    static const uint8_t literal_lengths[4] = {6U, 10U, 10U, 18U};
    static const uint8_t literal_bits[3][4] = {{1U, 1U, 1U, 1U}, {2U, 3U, 3U, 4U}, {4U, 5U, 7U, 14U}};
    uint16_t distance_values[2][4];
    uint8_t distance_bits[3][4];
    adfc_imp_bits bits;
    uint32_t end = packed_size - ADFC_IMP_TAIL, literals;
    size_t pos = raw_size;
    unsigned index;
    bool good = false;
    bits.data = data;
    bits.size = packed_size;
    bits.at = end;
    bits.reference = end;
    bits.word = 0U;
    bits.left = 0U;
    if (!(data[end + 16U] & 0x80U)) --bits.at;
    for (index = 0U; index < 7U; ++index) {
        uint8_t half = data[end + 17U];
        if (half & (1U << index)) {
            bits.word = (uint8_t)(half >> (index + 1U));
            bits.left = 7U - index;
            break;
        }
    }
    for (index = 0U; index < 8U; ++index) distance_values[index >> 2U][index & 3U] = (uint16_t)adfc_be16(data + end + 18U + index * 2U);
    for (index = 0U; index < 12U; ++index) distance_bits[index >> 2U][index & 3U] = data[end + 34U + index];
    literals = adfc_be32(data + end + 12U);
    for (;;) {
        uint32_t first, second, third, selector, count, more, step;
        uint64_t distance;
        if (literals > pos) break;
        for (step = 0U; step < literals; ++step) {
            uint8_t value;
            if (!adfc_imp_byte(&bits, &value)) goto done;
            out[--pos] = value;
        }
        if (!pos) {
            good = true;
            break;
        }
        for (first = 0U; first < 5U; ++first) { /* 0, 10, 110, 1110, 11110, 11111 */
            if (!adfc_imp_read(&bits, 1U, &more)) goto done;
            if (!more) break;
        }
        selector = first < 4U ? first : 3U;
        count = first + 2U;
        if (count == 6U) {
            if (!adfc_imp_read(&bits, 3U, &more)) break;
            count += more;
        } else if (count == 7U) {
            uint8_t value;
            if (!adfc_imp_byte(&bits, &value) || !value) break;
            count = value;
        }
        if (!adfc_imp_three(&bits, &second)) break;
        literals = second + second;
        if (literals == 4U) literals = literal_lengths[selector];
        if (!adfc_imp_read(&bits, literal_bits[second][selector], &more)) break;
        literals += more;
        if (!adfc_imp_three(&bits, &third)) break;
        if (distance_bits[third][selector] > 32U || !adfc_imp_read(&bits, distance_bits[third][selector], &more)) break;
        distance = UINT64_C(1) + (third ? distance_values[third - 1U][selector] : 0U) + more;
        if (count > pos || distance > raw_size - pos) break;
        for (step = 0U; step < count; ++step) {
            --pos;
            out[pos] = out[pos + (size_t)distance];
        }
    }
done:
    if (made) *made = raw_size - pos;
    return good;
}

/* ---------------------------------------------------------------------- */
/* Word-wise bit streams (Pack-Ice, StoneCracker)                          */

/* Bits come from words of 1, 2 or 4 bytes that are fetched towards lower
 * addresses (the word is still big-endian) or towards higher ones; a word
 * gives its bits from the top (msb) or from the bottom.  Running out of
 * bytes latches `error`. */
typedef struct adfc_bits_s {
    const uint8_t *data;
    size_t at;   /**< Backward: the next byte is data[at - 1]; forward: data[at]. */
    size_t stop; /**< Backward: the first byte of the stream; forward: its end. */
    uint32_t word;
    unsigned left;
    unsigned bytes;
    bool backward, msb, error;
} adfc_bits;

static uint32_t adfc_bits_byte(adfc_bits *bits)
{
    if (bits->error) return 0U;
    if (bits->backward) {
        if (bits->at <= bits->stop) {
            bits->error = true;
            return 0U;
        }
        return bits->data[--bits->at];
    }
    if (bits->at >= bits->stop) {
        bits->error = true;
        return 0U;
    }
    return bits->data[bits->at++];
}

static uint32_t adfc_bits_word(adfc_bits *bits, unsigned bytes)
{
    uint32_t value = 0U;
    unsigned index;
    for (index = 0U; index < bytes; ++index) {
        uint32_t byte = adfc_bits_byte(bits);
        value = bits->backward ? value | (byte << (8U * index)) : (value << 8U) | byte;
    }
    return bits->error ? 0U : value;
}

/* @p count is at most 16. */
static uint32_t adfc_bits_get(adfc_bits *bits, unsigned count)
{
    uint32_t result = 0U;
    unsigned position = 0U;
    while (count != 0U && !bits->error) {
        unsigned take;
        uint32_t mask;
        if (!bits->left) {
            bits->word = adfc_bits_word(bits, bits->bytes);
            if (bits->error) return 0U;
            bits->left = 8U * bits->bytes;
        }
        take = count < bits->left ? count : bits->left;
        mask = (UINT32_C(1) << take) - 1U;
        if (bits->msb) {
            bits->left -= take;
            result = (result << take) | ((bits->word >> bits->left) & mask);
        } else {
            result |= (bits->word & mask) << position;
            bits->word >>= take;
            bits->left -= take;
            position += take;
        }
        count -= take;
    }
    return bits->error ? 0U : result;
}

static uint32_t adfc_bits_mirror(uint32_t value, unsigned count)
{
    uint32_t result = 0U;
    while (count-- != 0U) {
        result = (result << 1U) | (value & 1U);
        value >>= 1U;
    }
    return result;
}

/* Lengths given as steps: each step that reads all ones passes on to the
 * next, the last step always ends. */
static uint32_t adfc_bits_cascade(adfc_bits *bits, const uint8_t *lengths, unsigned steps)
{
    uint32_t offset = 0U, value;
    unsigned index;
    for (index = 0U;; ++index) {
        value = adfc_bits_get(bits, lengths[index]);
        if (index == steps - 1U || value != (UINT32_C(1) << lengths[index]) - 1U) return offset - index + value;
        offset += UINT32_C(1) << lengths[index];
    }
}

/* The output is filled from its end (backward) or from its start. */
typedef struct adfc_out_s {
    uint8_t *data;
    size_t size;
    size_t pos; /**< Backward: bytes still to write; forward: bytes written. */
    bool backward, failed;
} adfc_out;

static void adfc_out_put(adfc_out *out, uint32_t value)
{
    if (out->failed) return;
    if (out->backward) {
        if (!out->pos) {
            out->failed = true;
            return;
        }
        out->data[--out->pos] = (uint8_t)value;
    } else {
        if (out->pos >= out->size) {
            out->failed = true;
            return;
        }
        out->data[out->pos++] = (uint8_t)value;
    }
}

static void adfc_out_copy(adfc_out *out, uint32_t distance, uint32_t count)
{
    if (out->failed) return;
    if (out->backward) {
        if (!distance || count > out->pos || distance > out->size - out->pos) {
            out->failed = true;
            return;
        }
        while (count-- != 0U) {
            --out->pos;
            out->data[out->pos] = out->data[out->pos + distance];
        }
    } else {
        if (!distance || distance > out->pos || count > out->size - out->pos) {
            out->failed = true;
            return;
        }
        while (count-- != 0U) {
            out->data[out->pos] = out->data[out->pos - distance];
            ++out->pos;
        }
    }
}

static bool adfc_out_full(const adfc_out *out)
{
    return out->backward ? out->pos == 0U : out->pos == out->size;
}

static uint64_t adfc_out_made(const adfc_out *out)
{
    return out->backward ? out->size - out->pos : out->pos;
}

/* ---------------------------------------------------------------------- */
/* Pack-Ice                                                                */

#define ADFC_ICE_HEADER 12U

static bool adfc_ice_tag(const uint8_t *tag)
{
    uint32_t id = adfc_be32(tag);
    return id == UINT32_C(0x49636521) /* Ice! */ || id == UINT32_C(0x49434521) /* ICE! */ || id == UINT32_C(0x544D4D21) /* TMM! */ || id == UINT32_C(0x54534D21) /* TSM! */ ||
           id == UINT32_C(0x53484521) /* SHE! */;
}

static bool adfc_ice_sizes(const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *total)
{
    if (limit < ADFC_ICE_HEADER) return false;
    *total = adfc_be32(data + 4U);
    *raw_size = adfc_be32(data + 8U);
    return *total > ADFC_ICE_HEADER && *total <= limit && *raw_size != 0U && *raw_size <= ADFC_MAX_RAW;
}

/* One reading of the stream: @p use_bytes takes it byte by byte (2.x), else
 * in longwords (1.x).  The stream must be used up. */
static bool adfc_ice_run(const uint8_t *data, uint32_t total, uint8_t *raw, uint32_t raw_size, bool use_bytes, uint64_t *made)
{
    static const uint8_t literal_bits[6] = {1U, 2U, 2U, 3U, 8U, 15U};
    static const uint8_t ones[4] = {1U, 1U, 1U, 1U};
    static const uint8_t count_bits[5] = {0U, 0U, 1U, 2U, 10U};
    static const uint8_t count_base[5] = {0U, 1U, 2U, 4U, 8U};
    static const uint8_t distance_bits[3] = {5U, 8U, 12U};
    static const uint16_t distance_base[3] = {0U, 32U, 288U};
    adfc_bits bits;
    adfc_out out;
    uint32_t value, anchor = 0U, tmp;
    bool good = false;
    bits.data = data;
    bits.at = total;
    bits.stop = ADFC_ICE_HEADER;
    bits.word = 0U;
    bits.left = 0U;
    bits.bytes = use_bytes ? 1U : 4U;
    bits.backward = true;
    bits.msb = true;
    bits.error = false;
    out.data = raw;
    out.size = raw_size;
    out.pos = raw_size;
    out.backward = true;
    out.failed = false;
    /* The lowest set bit of the first word marks where the bits start. */
    value = adfc_bits_word(&bits, bits.bytes);
    for (tmp = value; tmp; tmp <<= 1U) ++anchor;
    if (anchor) --anchor;
    if (anchor) {
        bits.word = value >> (32U - anchor);
        bits.left = anchor - (use_bytes ? 24U : 0U);
    }
    while (!bits.error && !out.failed) {
        uint32_t count, distance, base, index;
        if (adfc_bits_get(&bits, 1U)) {
            count = adfc_bits_cascade(&bits, literal_bits, 6U) + 1U;
            if (bits.error || count > out.pos) break;
            for (index = 0U; index < count; ++index) adfc_out_put(&out, adfc_bits_byte(&bits));
            if (bits.error) break;
        }
        if (bits.error) break;
        if (adfc_out_full(&out)) {
            good = true;
            break;
        }
        base = adfc_bits_cascade(&bits, ones, 4U);
        count = count_base[base] + adfc_bits_get(&bits, count_bits[base]) + 2U;
        if (count == 2U) {
            distance = adfc_bits_get(&bits, 1U) ? adfc_bits_get(&bits, 9U) + 0x40U : adfc_bits_get(&bits, 6U);
            distance += count - (use_bytes ? 1U : 0U);
        } else {
            base = adfc_bits_cascade(&bits, ones, 2U);
            if (base < 2U) base ^= 1U;
            distance = distance_base[base] + adfc_bits_get(&bits, distance_bits[base]);
            if (use_bytes) distance = distance ? distance + count - 1U : 1U;
            else distance += count;
        }
        if (bits.error) break;
        adfc_out_copy(&out, distance, count);
    }
    if (made) *made = adfc_out_made(&out);
    if (!good || bits.error || out.failed) return false;
    /* A picture stored as chunky words is turned back into bitplanes. */
    if (((bits.at - bits.stop) * 8U + bits.left) != 0U && adfc_bits_get(&bits, 1U)) {
        uint32_t size = 32000U, at;
        /* "ICE!" may give the size of the picture. */
        if (data[1] == 'C' && ((bits.at - bits.stop) * 8U + bits.left) >= 17U && adfc_bits_get(&bits, 1U)) size = adfc_bits_get(&bits, 16U) * 8U + 8U;
        if (bits.error || raw_size < size) return false;
        for (at = raw_size - size; at < raw_size; at += 8U) {
            uint16_t planes[4] = {0U, 0U, 0U, 0U};
            unsigned pair, bit;
            for (pair = 0U; pair < 8U; pair += 2U) {
                uint32_t word = adfc_be16(raw + at + 6U - pair);
                for (bit = 0U; bit < 16U; ++bit) {
                    planes[bit & 3U] = (uint16_t)((planes[bit & 3U] << 1U) | ((word >> 15U) & 1U));
                    word = (word << 1U) & 0xFFFFU;
                }
            }
            for (pair = 0U; pair < 4U; ++pair) {
                raw[at + pair * 2U] = (uint8_t)(planes[pair] >> 8U);
                raw[at + pair * 2U + 1U] = (uint8_t)planes[pair];
            }
        }
    }
    return !bits.error && bits.at == bits.stop;
}

/* "Ice!" covers two stream formats: the longword one is tried first. */
static bool adfc_ice_decode(const uint8_t *data, uint32_t total, uint8_t *raw, uint32_t raw_size, uint64_t *made)
{
    bool newest = data[1] == 'C';
    uint64_t first = 0U, second = 0U;
    bool good = !newest && adfc_ice_run(data, total, raw, raw_size, false, &first);
    if (!good) good = adfc_ice_run(data, total, raw, raw_size, true, &second);
    if (made) *made = first + second;
    return good;
}

/* ---------------------------------------------------------------------- */
/* StoneCracker                                                            */

/* The generation of a StoneCracker id (3: S300, 6: S401, 7: S403,
 * 8: S404), 0 when @p tag is none of them. */
static unsigned adfc_sc_generation(const uint8_t *tag)
{
    if (tag[0] != 'S') return 0U;
    if (tag[1] == '3' && tag[2] == '0' && tag[3] == '0') return 3U;
    if (tag[1] != '4' || tag[2] != '0') return 0U;
    return tag[3] == '1' ? 6U : tag[3] == '3' ? 7U : tag[3] == '4' ? 8U : 0U;
}

static bool adfc_sc_sizes(const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *total)
{
    unsigned generation = adfc_sc_generation(data), head, index;
    uint64_t packed;
    if (limit < 18U || !generation) return false;
    if (generation == 3U) {
        for (index = 4U; index < 8U; ++index) {
            if (data[index] < 8U || data[index] > 14U) return false;
        }
    }
    head = generation == 6U ? 12U : 16U;
    *raw_size = adfc_be32(data + (generation == 6U ? 4U : 8U));
    packed = (uint64_t)adfc_be32(data + (generation == 6U ? 8U : 12U)) + (generation >= 7U ? 2U : 0U);
    if (*raw_size == 0U || *raw_size > ADFC_MAX_RAW || packed < 4U || packed > limit - head) return false;
    *total = (uint32_t)packed + head;
    return true;
}

/* "Read a count in steps of @p width bits; a step equal to @p full adds
 * and goes on." */
static uint32_t adfc_sc_count(adfc_bits *bits, uint32_t full, unsigned width, bool mirror)
{
    uint32_t result = 0U, step;
    do {
        step = adfc_bits_get(bits, width);
        if (mirror) step = adfc_bits_mirror(step, width);
        result += step;
    } while (step == full && !bits->error && result <= ADFC_MAX_RAW);
    return result;
}

/* 3.00: read backwards in longwords, low bit first, values mirrored. */
static void adfc_sc_run3(adfc_bits *bits, adfc_out *out, const uint8_t *modes)
{
    uint32_t value = adfc_bits_word(bits, 4U);
    unsigned index;
    for (index = 31U; value && index > 0U; --index) {
        if (value & (UINT32_C(1) << index)) {
            bits->word = value & ((UINT32_C(1) << index) - 1U);
            bits->left = index;
            break;
        }
    }
    while (!bits->error && !out->failed && !adfc_out_full(out)) {
        uint32_t count, mode;
        unsigned width;
        if (adfc_bits_get(bits, 1U)) {
            count = adfc_sc_count(bits, 7U, 3U, true) + 1U;
            if (bits->error || count > out->pos) {
                out->failed = true;
                break;
            }
            while (count-- != 0U) adfc_out_put(out, adfc_bits_get(bits, 8U));
            continue;
        }
        mode = adfc_bits_mirror(adfc_bits_get(bits, 2U), 2U);
        width = modes[mode] + 1U;
        if (mode == 3U) {
            if (adfc_bits_get(bits, 1U)) {
                count = adfc_sc_count(bits, 7U, 3U, true) + 5U;
                width = 8U;
            } else {
                count = adfc_sc_count(bits, 127U, 7U, true) + 5U;
            }
        } else {
            count = mode + 2U;
        }
        value = adfc_bits_mirror(adfc_bits_get(bits, width), width) + 1U;
        if (bits->error) break;
        adfc_out_copy(out, value, count);
    }
}

/* 4.01: read forwards in words, high bit first. */
static void adfc_sc_run6(adfc_bits *bits, adfc_out *out)
{
    static const uint8_t modes[4] = {10U, 11U, 12U, 8U};
    while (!bits->error && !out->failed && !adfc_out_full(out)) {
        uint32_t count, distance, mode;
        if (adfc_bits_get(bits, 1U)) {
            mode = adfc_bits_get(bits, 2U);
            distance = adfc_bits_get(bits, modes[mode]);
            count = mode >= 2U ? adfc_sc_count(bits, 7U, 3U, false) + 3U : mode + 3U;
            if (bits->error) break;
            if (count > out->size - out->pos) count = (uint32_t)(out->size - out->pos);
            adfc_out_copy(out, distance, count);
        } else {
            count = adfc_sc_count(bits, 7U, 3U, false) + 1U;
            if (bits->error || count > out->size - out->pos) {
                out->failed = true;
                break;
            }
            while (count-- != 0U) adfc_out_put(out, adfc_bits_get(bits, 8U));
        }
    }
}

/* 4.02a: read backwards in words, low bit first. */
static void adfc_sc_run7(adfc_bits *bits, adfc_out *out)
{
    static const uint8_t distance_bits[4] = {5U, 8U, 10U, 12U};
    static const uint16_t distance_base[4] = {0U, 32U, 288U, 1312U};
    while (!bits->error && !out->failed && !adfc_out_full(out)) {
        uint32_t count = 2U, distance, step, index;
        if (!adfc_bits_get(bits, 1U)) {
            adfc_out_put(out, adfc_bits_get(bits, 8U));
            continue;
        }
        index = adfc_bits_get(bits, 2U);
        distance = distance_base[index] + adfc_bits_get(bits, distance_bits[index]) + 1U;
        if (!adfc_bits_get(bits, 1U)) {
            ++count;
            if (!adfc_bits_get(bits, 1U)) {
                ++count;
                if (!adfc_bits_get(bits, 1U)) {
                    ++count;
                    do {
                        step = adfc_bits_get(bits, 3U);
                        count += step;
                    } while (step == 7U && !bits->error && count <= ADFC_MAX_RAW);
                }
            }
        }
        if (bits->error) break;
        adfc_out_copy(out, distance, count);
    }
}

/* One symbol of a prefix code given as (length, code, value) rows; the
 * first bit read is the top bit of the code.  0xFF when nothing fits. */
static uint32_t adfc_sc_symbol(adfc_bits *bits, const uint8_t (*rows)[3], unsigned count)
{
    uint32_t code = 0U;
    unsigned depth, index;
    for (depth = 1U; depth <= 8U; ++depth) {
        code = (code << 1U) | adfc_bits_get(bits, 1U);
        for (index = 0U; index < count; ++index) {
            if (rows[index][0] == depth && rows[index][1] == code) return rows[index][2];
        }
    }
    bits->error = true;
    return 0xFFU;
}

/* 4.10: read backwards in words, high bit first. */
static void adfc_sc_run8(adfc_bits *bits, adfc_out *out, unsigned mode_bits)
{
    static const uint8_t count_rows[9][3] = {{1U, 0x00U, 0x80U}, {2U, 0x03U, 1U}, {3U, 0x05U, 2U}, {4U, 0x08U, 7U},  {5U, 0x12U, 3U},
                                             {6U, 0x26U, 4U},    {7U, 0x4EU, 5U}, {8U, 0x9EU, 6U}, {8U, 0x9FU, 0x8BU}};
    static const uint8_t distance_rows[3][3] = {{1U, 0x01U, 2U}, {2U, 0x00U, 1U}, {2U, 0x01U, 0U}};
    static const uint8_t count_bits[12] = {0U, 1U, 2U, 3U, 2U, 1U, 0U, 8U, 3U, 2U, 0U, 5U};
    static const uint8_t count_base[12] = {0U, 1U, 3U, 7U, 15U, 19U, 21U, 22U, 0U, 8U, 12U, 13U};
    while (!bits->error && !out->failed && !adfc_out_full(out)) {
        uint32_t symbol = adfc_sc_symbol(bits, count_rows, 9U), count, step, index, distance;
        if (bits->error || (symbol & 0x7FU) >= 12U) {
            out->failed = true;
            break;
        }
        count = count_base[symbol & 0x7FU] + adfc_bits_get(bits, count_bits[symbol & 0x7FU]) + 1U;
        if (count == 278U) {
            do {
                step = adfc_bits_get(bits, 8U);
                count += step;
            } while (step == 0xFFU && !bits->error && count <= ADFC_MAX_RAW);
        }
        if (bits->error) break;
        if (symbol & 0x80U) {
            if (count > out->pos) {
                out->failed = true;
                break;
            }
            while (count-- != 0U) adfc_out_put(out, adfc_bits_get(bits, 8U));
            continue;
        }
        index = adfc_sc_symbol(bits, distance_rows, 3U);
        if (bits->error) break;
        distance = (index == 0U ? 0U : index == 1U ? 32U : 544U) + adfc_bits_get(bits, index == 0U ? 5U : index == 1U ? 9U : mode_bits) + 1U;
        if (bits->error) break;
        adfc_out_copy(out, distance, count);
    }
}

static bool adfc_sc_decode(const uint8_t *data, uint32_t total, uint8_t *raw, uint32_t raw_size, uint64_t *made)
{
    unsigned generation = adfc_sc_generation(data);
    adfc_bits bits;
    adfc_out out;
    bool good;
    bits.data = data;
    bits.word = 0U;
    bits.left = 0U;
    bits.error = false;
    out.data = raw;
    out.size = raw_size;
    out.failed = false;
    out.backward = generation != 6U;
    out.pos = out.backward ? raw_size : 0U;
    bits.backward = out.backward;
    if (generation == 3U) {
        bits.at = total;
        bits.stop = 16U;
        bits.bytes = 4U;
        bits.msb = false;
        adfc_sc_run3(&bits, &out, data + 4U);
    } else if (generation == 6U) {
        bits.at = 12U;
        bits.stop = total;
        bits.bytes = 2U;
        bits.msb = true;
        adfc_sc_run6(&bits, &out);
    } else {
        /* The last word of the file counts the bits of the word before it. */
        uint32_t count = adfc_be16(data + total - 2U), value;
        bits.at = total - 2U;
        bits.stop = 16U;
        bits.bytes = 2U;
        bits.msb = generation == 8U;
        value = adfc_bits_word(&bits, 2U);
        if (generation == 7U) {
            if (count > 16U) bits.error = true;
            bits.word = value;
            bits.left = count;
            if (!bits.error) adfc_sc_run7(&bits, &out);
        } else {
            uint32_t mode_bits;
            count &= 15U;
            bits.word = value >> (16U - count);
            bits.left = count;
            mode_bits = adfc_bits_word(&bits, 2U);
            if (mode_bits < 10U || mode_bits > 14U) bits.error = true;
            if (!bits.error) adfc_sc_run8(&bits, &out, mode_bits);
        }
    }
    if (made) *made = adfc_out_made(&out);
    good = !bits.error && !out.failed && adfc_out_full(&out);
    if (!good) return false;
    /* Measured: the backward formats end on the first bit of their stream;
     * 4.01 leaves up to four bytes of padding. */
    if (generation == 6U) return bits.stop - bits.at <= 4U;
    return bits.at == bits.stop && bits.left == 0U;
}

/* ---------------------------------------------------------------------- */
/* Any of them                                                             */

/* Which cruncher's id stands at @p tag (four bytes). */
static unsigned adfc_tag(const uint8_t *tag)
{
    switch (tag[0]) {
        case 'P':
            return tag[1] == 'P' && tag[2] == '2' && tag[3] == '0' ? ADFC_PP20 : ADFC_NONE;
        case 'R':
            return tag[1] == 'N' && tag[2] == 'C' && (tag[3] == 1U || tag[3] == 2U) ? ADFC_RNC : ADFC_NONE;
        case 'C':
            if (tag[1] == 'r' && (tag[2] == 'M' || tag[2] == 'm') && (tag[3] == '!' || tag[3] == '2')) return ADFC_CRM;
            return adfc_imp_addition(tag) ? ADFC_IMP : ADFC_NONE;
        case 'I':
            if (adfc_ice_tag(tag)) return ADFC_ICE;
            return adfc_imp_addition(tag) ? ADFC_IMP : ADFC_NONE;
        case 'T':
            return adfc_ice_tag(tag) ? ADFC_ICE : ADFC_NONE;
        case 'S':
            if (adfc_ice_tag(tag)) return ADFC_ICE;
            return adfc_sc_generation(tag) ? ADFC_SC : ADFC_NONE;
        case 'A':
        case 'E':
        case 'M':
        case 'B':
            return adfc_imp_addition(tag) ? ADFC_IMP : ADFC_NONE;
        default:
            return ADFC_NONE;
    }
}

/* Sizes of the crunched file at data[0], from its header alone; PowerPacker
 * has none and is not asked here. */
static bool adfc_sizes(unsigned kind, const uint8_t *data, size_t limit, uint32_t *raw_size, uint32_t *total)
{
    uint32_t packed = 0U;
    switch (kind) {
        case ADFC_RNC:
            if (!adfc_rnc_sizes(data, limit, raw_size, &packed)) return false;
            *total = packed + ADFC_RNC_HEADER;
            return true;
        case ADFC_CRM:
            if (!adfc_crm_sizes(data, limit, raw_size, &packed)) return false;
            *total = packed + ADFC_CRM_HEADER;
            return true;
        case ADFC_IMP:
            return adfc_imp_sizes(data, limit, raw_size, total);
        case ADFC_ICE:
            return adfc_ice_sizes(data, limit, raw_size, total);
        case ADFC_SC:
            return adfc_sc_sizes(data, limit, raw_size, total);
        default:
            return false;
    }
}

/* Decode the crunched file data[0..packed_size) of a known kind into
 * out[0..raw_size).  The header is checked again, so the call is safe on
 * bytes that are not what adfc_open saw. */
static bool adfc_decode(unsigned kind, const uint8_t *data, uint32_t packed_size, uint8_t *out, uint32_t raw_size, uint64_t *made)
{
    uint32_t raw = 0U, total = 0U;
    size_t produced = 0U;
    uint64_t left = ADFC_PP_MAX_LEFT + 1U;
    bool good;
    if (made) *made = 0U;
    if (raw_size == 0U || raw_size > ADFC_MAX_RAW) return false;
    if (kind == ADFC_PP20) {
        if (packed_size < ADFC_PP_HEADER + 8U || (packed_size & 3U) != 0U || !adfc_pp_widths_ok(data + 4U) || data[packed_size - 1U] > ADFC_PP_MAX_SKIP ||
            (((uint32_t)data[packed_size - 4U] << 16U) | ((uint32_t)data[packed_size - 3U] << 8U) | (uint32_t)data[packed_size - 2U]) != raw_size)
            return false;
        good = adfc_pp_decode(data, ADFC_PP_HEADER, packed_size - 4U, data + 4U, data[packed_size - 1U], out, raw_size, &produced, &left) && left <= ADFC_PP_MAX_LEFT;
        if (made) *made = produced;
        return good;
    }
    if (!adfc_sizes(kind, data, packed_size, &raw, &total) || raw != raw_size || total != packed_size) return false;
    if (kind == ADFC_RNC) return adfc_crc16(data + ADFC_RNC_HEADER, packed_size - ADFC_RNC_HEADER) == adfc_be16(data + 14U) && adfc_rnc_decode(data, packed_size - ADFC_RNC_HEADER, out, raw_size, made);
    if (kind == ADFC_CRM) return adfc_crm_decode(data, packed_size - ADFC_CRM_HEADER, out, raw_size, made);
    if (kind == ADFC_ICE) return adfc_ice_decode(data, packed_size, out, raw_size, made);
    if (kind == ADFC_SC) return adfc_sc_decode(data, packed_size, out, raw_size, made);
    return adfc_imp_sum_ok(data, packed_size) && adfc_imp_decode(data, packed_size, out, raw_size, made);
}

/* Recognise and decrunch the file that starts at data[0].  Returns its
 * crunched length and hands out the plain bytes (*plain, to be freed with
 * xx_mem_free, *raw_size long); 0 when there is no complete file. */
static uint32_t adfc_open(const uint8_t *data, size_t limit, unsigned *kind, uint8_t **plain, uint32_t *raw_size, adfc_state *state)
{
    uint32_t total = 0U;
    uint64_t made = 0U;
    bool good;
    *plain = NULL;
    *raw_size = 0U;
    if (limit < 4U || (*kind = adfc_tag(data)) == ADFC_NONE || state->work > ADFC_MAX_WORK) return 0U;
    if (*kind == ADFC_PP20) return adfc_pp_open(data, limit, plain, raw_size, state);
    if (!adfc_sizes(*kind, data, limit, raw_size, &total)) return 0U;
    state->work += total;
    *plain = (uint8_t *)xx_mem_alloc(*raw_size);
    if (!*plain) return 0U;
    good = adfc_decode(*kind, data, total, *plain, *raw_size, &made);
    state->work += made;
    if (!good) {
        xx_mem_free(*plain);
        *plain = NULL;
        *raw_size = 0U;
        return 0U;
    }
    return total;
}

#endif /* XX_ADF_CRUNCH_H */
