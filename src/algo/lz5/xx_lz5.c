/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Independently written LZ5 frame and sequence decoder. It implements the LZ5
 * token grammar (10/16/24-bit and repeated offsets) directly and does not link
 * to liblz5 or XArchive.
 */

#include "xxfclib/algo/lz5/xx_lz5.h"

#include <stddef.h>
#include <stdint.h>

#define XX_LZ5_MAGIC UINT32_C(0x184D2205)
#define XX_LZ5_SKIP_MAGIC UINT32_C(0x184D2A50)

#define XX_LZ5_XXH_P1 UINT32_C(2654435761)
#define XX_LZ5_XXH_P2 UINT32_C(2246822519)
#define XX_LZ5_XXH_P3 UINT32_C(3266489917)
#define XX_LZ5_XXH_P4 UINT32_C(668265263)
#define XX_LZ5_XXH_P5 UINT32_C(374761393)

static uint32_t xx_lz5_read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_lz5_read64(const uint8_t *p) {
    return (uint64_t)xx_lz5_read32(p) |
           ((uint64_t)xx_lz5_read32(p + 4) << 32);
}

static uint32_t xx_lz5_rotl(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32U - bits));
}

static uint32_t xx_lz5_xxh_round(uint32_t state, uint32_t word) {
    state += word * XX_LZ5_XXH_P2;
    state = xx_lz5_rotl(state, 13);
    return state * XX_LZ5_XXH_P1;
}

static uint32_t xx_lz5_xxh32(const uint8_t *data, size_t size) {
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint32_t hash;

    if (size >= 16U) {
        const uint8_t *limit = end - 16U;
        uint32_t a = XX_LZ5_XXH_P1 + XX_LZ5_XXH_P2;
        uint32_t b = XX_LZ5_XXH_P2;
        uint32_t c = 0;
        uint32_t d = 0U - XX_LZ5_XXH_P1;
        do {
            a = xx_lz5_xxh_round(a, xx_lz5_read32(cursor));
            b = xx_lz5_xxh_round(b, xx_lz5_read32(cursor + 4));
            c = xx_lz5_xxh_round(c, xx_lz5_read32(cursor + 8));
            d = xx_lz5_xxh_round(d, xx_lz5_read32(cursor + 12));
            cursor += 16;
        } while (cursor <= limit);
        hash = xx_lz5_rotl(a, 1) + xx_lz5_rotl(b, 7) +
               xx_lz5_rotl(c, 12) + xx_lz5_rotl(d, 18);
    } else {
        hash = XX_LZ5_XXH_P5;
    }
    hash += (uint32_t)size;
    while ((size_t)(end - cursor) >= 4U) {
        hash += xx_lz5_read32(cursor) * XX_LZ5_XXH_P3;
        hash = xx_lz5_rotl(hash, 17) * XX_LZ5_XXH_P4;
        cursor += 4;
    }
    while (cursor < end) {
        hash += (uint32_t)(*cursor++) * XX_LZ5_XXH_P5;
        hash = xx_lz5_rotl(hash, 11) * XX_LZ5_XXH_P1;
    }
    hash ^= hash >> 15;
    hash *= XX_LZ5_XXH_P2;
    hash ^= hash >> 13;
    hash *= XX_LZ5_XXH_P3;
    return hash ^ (hash >> 16);
}

static bool xx_lz5_add_length(const uint8_t **cursor, const uint8_t *end,
                              size_t *length) {
    uint8_t value;
    do {
        if (*cursor == end) return false;
        value = *(*cursor)++;
        if (*length > SIZE_MAX - value) return false;
        *length += value;
    } while (value == UINT8_C(255));
    return true;
}

static bool xx_lz5_decode_block(const uint8_t *source, size_t source_size,
                                uint8_t *destination, size_t destination_capacity,
                                size_t history_size, size_t *out_written) {
    const uint8_t *input = source;
    const uint8_t *end = source + source_size;
    size_t output = history_size;
    size_t last_distance = 1U;

    if (out_written) *out_written = 0;
    if (history_size > destination_capacity) return false;

    while (input < end) {
        uint8_t token = *input++;
        size_t literal_length;
        size_t match_length;
        size_t distance;
        size_t match;

        if ((token >> 6) != 0U) {
            literal_length = (size_t)((token >> 3) & 3U);
            if (literal_length == 3U &&
                !xx_lz5_add_length(&input, end, &literal_length)) {
                return false;
            }
        } else {
            literal_length = (size_t)((token >> 3) & 7U);
            if (literal_length == 7U &&
                !xx_lz5_add_length(&input, end, &literal_length)) {
                return false;
            }
        }

        if (literal_length > (size_t)(end - input) ||
            literal_length > destination_capacity - output) {
            return false;
        }
        for (size_t index = 0; index < literal_length; ++index) {
            destination[output + index] = input[index];
        }
        input += literal_length;
        output += literal_length;
        if (input == end) {
            if (out_written) *out_written = output - history_size;
            return true;
        }

        if ((token & UINT8_C(0x80)) != 0U) {
            if (input == end) return false;
            distance = (size_t)*input++ | (size_t)((token >> 5) & 3U) << 8;
        } else if ((token >> 6) == 0U) {
            if ((size_t)(end - input) < 2U) return false;
            distance = (size_t)input[0] | ((size_t)input[1] << 8);
            input += 2;
        } else if ((token >> 5) == 2U) {
            if ((size_t)(end - input) < 3U) return false;
            distance = (size_t)input[0] | ((size_t)input[1] << 8) |
                       ((size_t)input[2] << 16);
            input += 3;
        } else {
            distance = last_distance;
        }
        if (distance == 0U || distance > output) return false;
        last_distance = distance;

        match_length = (size_t)(token & 7U);
        if (match_length == 7U &&
            !xx_lz5_add_length(&input, end, &match_length)) {
            return false;
        }
        if (match_length > SIZE_MAX - 3U) return false;
        match_length += 3U;
        if (match_length > destination_capacity - output) return false;
        match = output - distance;
        for (size_t index = 0; index < match_length; ++index) {
            destination[output++] = destination[match++];
        }
    }

    if (out_written) *out_written = output - history_size;
    return true;
}

static bool xx_lz5_block_limit(uint8_t descriptor, size_t *limit) {
    unsigned id = (descriptor >> 4) & 7U;
    size_t value = 64U * 1024U;
    if (id == 0U) return false;
    for (unsigned index = 1; index < id; ++index) {
        if (value > SIZE_MAX / 4U) return false;
        value *= 4U;
    }
    *limit = value;
    return true;
}

bool xx_lz5_decompress_memory(const void *source, size_t source_size,
                              void *destination, size_t destination_size,
                              size_t *out_written) {
    const uint8_t *input = (const uint8_t *)source;
    const uint8_t *end;
    uint8_t *output = (uint8_t *)destination;
    uint8_t empty_output = 0;
    size_t output_position = 0;
    bool saw_frame = false;

    if (out_written) *out_written = 0;
    if ((!input && source_size != 0U) || (!output && destination_size != 0U)) {
        return false;
    }
    if (!output) output = &empty_output;
    end = input + source_size;

    while (input < end) {
        uint32_t magic;
        const uint8_t *descriptor_start;
        uint8_t flags;
        uint8_t descriptor;
        size_t block_limit;
        size_t frame_output_start;
        uint64_t content_size = 0;
        bool independent;
        bool block_checksum;
        bool has_content_size;
        bool content_checksum;

        if ((size_t)(end - input) < 4U) return false;
        magic = xx_lz5_read32(input);
        input += 4;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_LZ5_SKIP_MAGIC) {
            uint32_t skip_size;
            if ((size_t)(end - input) < 4U) return false;
            skip_size = xx_lz5_read32(input);
            input += 4;
            if ((size_t)(end - input) < (size_t)skip_size) return false;
            input += skip_size;
            continue;
        }
        if (magic != XX_LZ5_MAGIC || (size_t)(end - input) < 3U) return false;

        descriptor_start = input;
        flags = *input++;
        descriptor = *input++;
        if ((flags >> 6) != 1U || (flags & 3U) != 0U ||
            (descriptor & UINT8_C(0x8F)) != 0U ||
            !xx_lz5_block_limit(descriptor, &block_limit)) {
            return false;
        }
        independent = (flags & UINT8_C(0x20)) != 0U;
        block_checksum = (flags & UINT8_C(0x10)) != 0U;
        has_content_size = (flags & UINT8_C(0x08)) != 0U;
        content_checksum = (flags & UINT8_C(0x04)) != 0U;
        if (has_content_size) {
            if ((size_t)(end - input) < 8U) return false;
            content_size = xx_lz5_read64(input);
            input += 8;
        }
        if (input == end ||
            *input != (uint8_t)(xx_lz5_xxh32(
                descriptor_start, (size_t)(input - descriptor_start)) >> 8)) {
            return false;
        }
        ++input;
        frame_output_start = output_position;

        for (;;) {
            uint32_t stored_size;
            size_t block_size;
            bool uncompressed;
            const uint8_t *block_data;
            size_t block_output = 0;

            if ((size_t)(end - input) < 4U) return false;
            stored_size = xx_lz5_read32(input);
            input += 4;
            if (stored_size == 0U) break;
            uncompressed = (stored_size & UINT32_C(0x80000000)) != 0U;
            block_size = (size_t)(stored_size & UINT32_C(0x7FFFFFFF));
            if (block_size == 0U || block_size > block_limit ||
                (size_t)(end - input) < block_size) {
                return false;
            }
            block_data = input;
            input += block_size;
            if (block_checksum) {
                if ((size_t)(end - input) < 4U ||
                    xx_lz5_read32(input) != xx_lz5_xxh32(block_data, block_size)) {
                    return false;
                }
                input += 4;
            }

            if (uncompressed) {
                if (block_size > destination_size - output_position) return false;
                for (size_t index = 0; index < block_size; ++index) {
                    output[output_position + index] = block_data[index];
                }
                block_output = block_size;
            } else if (independent) {
                if (!xx_lz5_decode_block(
                        block_data, block_size, output + output_position,
                        destination_size - output_position, 0, &block_output) ||
                    block_output > block_limit) {
                    return false;
                }
            } else {
                size_t history = output_position - frame_output_start;
                if (!xx_lz5_decode_block(
                        block_data, block_size, output + frame_output_start,
                        destination_size - frame_output_start, history,
                        &block_output) || block_output > block_limit) {
                    return false;
                }
            }
            output_position += block_output;
        }

        if (content_checksum) {
            if ((size_t)(end - input) < 4U ||
                xx_lz5_read32(input) != xx_lz5_xxh32(
                    output + frame_output_start,
                    output_position - frame_output_start)) {
                return false;
            }
            input += 4;
        }
        if (has_content_size &&
            (content_size > SIZE_MAX ||
             (size_t)content_size != output_position - frame_output_start)) {
            return false;
        }
        saw_frame = true;
    }

    if (!saw_frame || output_position != destination_size) return false;
    if (out_written) *out_written = output_position;
    return true;
}
