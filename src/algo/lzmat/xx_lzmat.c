/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Checked raw LZMAT grammar, independently implemented with nibble reads.
 * No wide input lookahead, allocator, container header, or target magic.
 */
#include "xxfclib/algo/lzmat/xx_lzmat.h"

typedef struct xx_lzmat_reader {
    const uint8_t *bytes;
    size_t size, position;
    unsigned half;
} xx_lzmat_reader;

static bool xx_lzmat_nibble(xx_lzmat_reader *r, uint32_t *value)
{
    if (r->position >= r->size) return false;
    *value = (r->bytes[r->position] >> (r->half * 4U)) & 0xfU;
    if (r->half) {
        ++r->position;
        r->half = 0U;
    } else r->half = 1U;
    return true;
}

static bool xx_lzmat_field(xx_lzmat_reader *r, unsigned nibbles, uint32_t *value)
{
    unsigned i;
    uint32_t part;
    *value = 0U;
    for (i = 0U; i < nibbles; ++i) {
        if (!xx_lzmat_nibble(r, &part)) return false;
        *value |= part << (i * 4U);
    }
    return true;
}

static bool xx_lzmat_octet_available(const xx_lzmat_reader *r)
{
    return r->position < r->size && (!r->half || r->size - r->position >= 2U);
}

static bool xx_lzmat_overlaps(const uint8_t *input, size_t input_size, const uint8_t *output, size_t output_size)
{
    uintptr_t a = (uintptr_t)input, b = (uintptr_t)output;
    return a <= b ? b - a < input_size : a - b < output_size;
}

bool xx_lzmat_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_capacity, size_t *written)
{
    xx_lzmat_reader r;
    size_t used = 1U;
    if (!written) return false;
    *written = 0U;
    if (!input || !input_size || !output || !output_capacity || xx_lzmat_overlaps(input, input_size, output, output_capacity)) return false;
    r.bytes = input;
    r.size = input_size;
    r.position = 1U;
    r.half = 0U;
    output[0] = input[0];
    while (xx_lzmat_octet_available(&r)) {
        uint32_t tag;
        unsigned bit;
        if (!xx_lzmat_field(&r, 2U, &tag)) return false;
        for (bit = 0U; bit < 8U && xx_lzmat_octet_available(&r); ++bit, tag = (tag << 1U) & 0xffU) {
            if (tag & 0x80U) {
                uint32_t code, extra, length, distance;
                unsigned kind, more, i;
                if (!xx_lzmat_field(&r, 2U, &code)) return false;
                kind = used < 0x881U ? code & 1U : code & 3U;
                more = used < 0x881U ? (kind ? 1U : 0U) : kind;
                for (i = 0U; i < more; ++i) {
                    if (!xx_lzmat_nibble(&r, &extra)) return false;
                    code |= extra << (8U + i * 4U);
                }
                if (used < 0x881U) {
                    distance = kind ? ((code >> 1U) & 0x7ffU) + 0x81U : (code >> 1U) + 1U;
                } else {
                    static const uint32_t bias[4] = {1U, 0x41U, 0x441U, 0x4441U};
                    distance = (code >> 2U) + bias[kind];
                }
                if (!xx_lzmat_nibble(&r, &length)) return false;
                if (length != 0xfU) length += 3U;
                else {
                    if (!xx_lzmat_field(&r, 2U, &length)) return false;
                    if (length != 0xffU) length += 0x12U;
                    else {
                        if (!xx_lzmat_field(&r, 4U, &length)) return false;
                        if (length == 0xffffU) {
                            uint32_t high;
                            size_t count, k;
                            /* The raw-block escape reuses bits of its earlier
                             * selector and discards the current tag group. */
                            if (r.half) {
                                if (r.position < 4U || r.position >= r.size) return false;
                                high = (r.bytes[r.position - 4U] & 0xfcU) << 5U;
                                ++r.position;
                                r.half = 0U;
                            } else {
                                if (r.position < 5U) return false;
                                high = ((uint32_t)r.bytes[r.position - 5U] | ((uint32_t)r.bytes[r.position - 4U] << 8U)) & 0xfc0U;
                                high <<= 1U;
                            }
                            count = (size_t)(high + (tag & 0x7fU) + 4U) * 8U;
                            if (count > r.size - r.position || count > output_capacity - used) return false;
                            for (k = 0U; k < count; ++k) output[used + k] = r.bytes[r.position + k];
                            r.position += count;
                            used += count;
                            break;
                        }
                        length += 0x111U;
                    }
                }
                if (!distance || distance > used || length > output_capacity - used) return false;
                while (length) {
                    output[used] = output[used - distance];
                    ++used;
                    --length;
                }
            } else {
                uint32_t literal;
                if (used == output_capacity || !xx_lzmat_field(&r, 2U, &literal)) return false;
                output[used++] = (uint8_t)literal;
            }
        }
    }
    *written = used;
    return true;
}
