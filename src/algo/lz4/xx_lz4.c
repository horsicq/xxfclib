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
 * Independently written LZ4 frame and block decoder. The implementation follows
 * the public LZ4 frame and block format descriptions and has no dependency on
 * liblz4 or XArchive.
 */

#include "xxfclib/algo/lz4/xx_lz4.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define XX_LZ4_FRAME_MAGIC UINT32_C(0x184D2204)
#define XX_LZ4_SKIP_MAGIC  UINT32_C(0x184D2A50)

#define XXH32_PRIME1 UINT32_C(2654435761)
#define XXH32_PRIME2 UINT32_C(2246822519)
#define XXH32_PRIME3 UINT32_C(3266489917)
#define XXH32_PRIME4 UINT32_C(668265263)
#define XXH32_PRIME5 UINT32_C(374761393)

static uint32_t xx_lz4_read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_lz4_read64(const uint8_t *p) {
    return (uint64_t)xx_lz4_read32(p) |
           ((uint64_t)xx_lz4_read32(p + 4) << 32);
}

static uint32_t xx_lz4_rotl32(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t xx_lz4_xxh_round(uint32_t state, uint32_t input) {
    state += input * XXH32_PRIME2;
    state = xx_lz4_rotl32(state, 13);
    return state * XXH32_PRIME1;
}

static uint32_t xx_lz4_xxh32(const uint8_t *data, size_t size) {
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint32_t hash;

    if (size >= 16U) {
        const uint8_t *limit = end - 16U;
        uint32_t lane1 = XXH32_PRIME1 + XXH32_PRIME2;
        uint32_t lane2 = XXH32_PRIME2;
        uint32_t lane3 = 0;
        uint32_t lane4 = 0U - XXH32_PRIME1;
        do {
            lane1 = xx_lz4_xxh_round(lane1, xx_lz4_read32(cursor));
            lane2 = xx_lz4_xxh_round(lane2, xx_lz4_read32(cursor + 4));
            lane3 = xx_lz4_xxh_round(lane3, xx_lz4_read32(cursor + 8));
            lane4 = xx_lz4_xxh_round(lane4, xx_lz4_read32(cursor + 12));
            cursor += 16;
        } while (cursor <= limit);
        hash = xx_lz4_rotl32(lane1, 1) + xx_lz4_rotl32(lane2, 7) +
               xx_lz4_rotl32(lane3, 12) + xx_lz4_rotl32(lane4, 18);
    } else {
        hash = XXH32_PRIME5;
    }

    hash += (uint32_t)size;
    while ((size_t)(end - cursor) >= 4U) {
        hash += xx_lz4_read32(cursor) * XXH32_PRIME3;
        hash = xx_lz4_rotl32(hash, 17) * XXH32_PRIME4;
        cursor += 4;
    }
    while (cursor < end) {
        hash += (uint32_t)(*cursor++) * XXH32_PRIME5;
        hash = xx_lz4_rotl32(hash, 11) * XXH32_PRIME1;
    }
    hash ^= hash >> 15;
    hash *= XXH32_PRIME2;
    hash ^= hash >> 13;
    hash *= XXH32_PRIME3;
    hash ^= hash >> 16;
    return hash;
}

static bool xx_lz4_extended_length(const uint8_t **cursor,
                                   const uint8_t *end, size_t *length) {
    uint8_t value;
    do {
        if (*cursor == end) return false;
        value = *(*cursor)++;
        if (*length > SIZE_MAX - value) return false;
        *length += value;
    } while (value == UINT8_C(255));
    return true;
}

static bool xx_lz4_decode_block(const uint8_t *source, size_t source_size,
                                uint8_t *destination, size_t destination_capacity,
                                size_t history_size, size_t *out_written) {
    const uint8_t *input = source;
    const uint8_t *input_end = source + source_size;
    size_t output = history_size;
    const size_t output_limit = destination_capacity;

    if (out_written) *out_written = 0;
    if (history_size > destination_capacity) return false;

    while (input < input_end) {
        uint8_t token = *input++;
        size_t literal_length = (size_t)(token >> 4);
        size_t match_length;
        size_t distance;
        size_t match;

        if (literal_length == 15U &&
            !xx_lz4_extended_length(&input, input_end, &literal_length)) {
            return false;
        }
        if (literal_length > (size_t)(input_end - input) ||
            literal_length > output_limit - output) {
            return false;
        }
        for (size_t index = 0; index < literal_length; ++index) {
            destination[output + index] = input[index];
        }
        input += literal_length;
        output += literal_length;

        if (input == input_end) {
            if (out_written) *out_written = output - history_size;
            return true;
        }
        if ((size_t)(input_end - input) < 2U) return false;
        distance = (size_t)input[0] | ((size_t)input[1] << 8);
        input += 2;
        if (distance == 0U || distance > output) return false;

        match_length = (size_t)(token & 15U);
        if (match_length == 15U &&
            !xx_lz4_extended_length(&input, input_end, &match_length)) {
            return false;
        }
        if (match_length > SIZE_MAX - 4U) return false;
        match_length += 4U;
        if (match_length > output_limit - output) return false;
        match = output - distance;
        for (size_t index = 0; index < match_length; ++index) {
            destination[output++] = destination[match++];
        }
    }

    if (out_written) *out_written = output - history_size;
    return true;
}

static bool xx_lz4_block_limit(uint8_t descriptor, size_t *limit) {
    switch ((descriptor >> 4) & 7U) {
        case 4: *limit = 64U * 1024U; return true;
        case 5: *limit = 256U * 1024U; return true;
        case 6: *limit = 1024U * 1024U; return true;
        case 7: *limit = 4U * 1024U * 1024U; return true;
        default: return false;
    }
}

static bool xx_lz4_decompress_frames_impl(const void *source,
                                          size_t source_size,
                                          void *destination,
                                          size_t destination_size,
                                          bool exact, size_t *out_written);

bool xx_lz4_decompress_memory(const void *source, size_t source_size,
                              void *destination, size_t destination_size,
                              size_t *out_written) {
    return xx_lz4_decompress_frames_impl(source, source_size, destination,
                                         destination_size, true, out_written);
}

bool xx_lz4_decompress_frames(const void *source, size_t source_size,
                              void *destination, size_t destination_capacity,
                              size_t *out_written) {
    return xx_lz4_decompress_frames_impl(source, source_size, destination,
                                         destination_capacity, false,
                                         out_written);
}

bool xx_lz4_decompress_block(const void *source, size_t source_size,
                             void *destination, size_t destination_capacity,
                             size_t *out_written) {
    /* A raw block starts with no history: the internal decoder carries a
     * history_size because the frame path chains blocks through a shared
     * window, which a standalone block never does. */
    if (out_written) *out_written = 0;
    if (!source || !destination) return false;
    return xx_lz4_decode_block((const uint8_t *)source, source_size,
                               (uint8_t *)destination, destination_capacity,
                               0U, out_written);
}

static bool xx_lz4_decompress_frames_impl(const void *source,
                                          size_t source_size,
                                          void *destination,
                                          size_t destination_size, bool exact,
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
        uint8_t block_descriptor;
        size_t block_limit;
        size_t frame_output_start;
        uint64_t content_size = 0;
        bool has_content_size;
        bool independent_blocks;
        bool block_checksum;
        bool content_checksum;

        if ((size_t)(end - input) < 4U) return false;
        magic = xx_lz4_read32(input);
        input += 4;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_LZ4_SKIP_MAGIC) {
            uint32_t skip_size;
            if ((size_t)(end - input) < 4U) return false;
            skip_size = xx_lz4_read32(input);
            input += 4;
            if ((size_t)(end - input) < (size_t)skip_size) return false;
            input += skip_size;
            continue;
        }
        if (magic != XX_LZ4_FRAME_MAGIC || (size_t)(end - input) < 3U) {
            return false;
        }

        descriptor_start = input;
        flags = *input++;
        block_descriptor = *input++;
        if ((flags >> 6) != 1U || (flags & 2U) != 0U ||
            (block_descriptor & UINT8_C(0x8F)) != 0U ||
            !xx_lz4_block_limit(block_descriptor, &block_limit)) {
            return false;
        }
        independent_blocks = (flags & UINT8_C(0x20)) != 0U;
        block_checksum = (flags & UINT8_C(0x10)) != 0U;
        has_content_size = (flags & UINT8_C(0x08)) != 0U;
        content_checksum = (flags & UINT8_C(0x04)) != 0U;

        if (has_content_size) {
            if ((size_t)(end - input) < 8U) return false;
            content_size = xx_lz4_read64(input);
            input += 8;
        }
        if ((flags & 1U) != 0U) {
            /* Dictionary IDs require an external dictionary, which this API
             * intentionally does not accept. */
            return false;
        }
        if (input == end ||
            *input != (uint8_t)(xx_lz4_xxh32(
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
            size_t history_size;

            if ((size_t)(end - input) < 4U) return false;
            stored_size = xx_lz4_read32(input);
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
                    xx_lz4_read32(input) != xx_lz4_xxh32(block_data, block_size)) {
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
            } else {
                uint8_t *block_destination;
                size_t block_capacity;
                if (independent_blocks) {
                    history_size = 0;
                    block_destination = output + output_position;
                    block_capacity = destination_size - output_position;
                } else {
                    history_size = output_position - frame_output_start;
                    block_destination = output + frame_output_start;
                    block_capacity = destination_size - frame_output_start;
                }
                if (!xx_lz4_decode_block(
                        block_data, block_size, block_destination,
                        block_capacity, history_size,
                        &block_output) || block_output > block_limit) {
                    return false;
                }
            }
            output_position += block_output;
        }

        if (content_checksum) {
            if ((size_t)(end - input) < 4U ||
                xx_lz4_read32(input) != xx_lz4_xxh32(
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

    if (!saw_frame) return false;
    /* `exact` is the historical contract: the caller knew the output
     * length up front and a mismatch means corruption. When the length
     * is not knowable -- an LZ4 frame need not carry a content size, and
     * the lz4 CLI omits it -- the destination is a capacity instead and
     * the real length is reported back. */
    if (exact && output_position != destination_size) return false;
    if (out_written) *out_written = output_position;
    return true;
}
