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
 * Independently written Lizard frame and sequence decoder. Lizard divides each
 * compressed block into flags, offsets, and literals streams; this module parses
 * those streams explicitly instead of depending on liblizard or XArchive.
 */

#include "xxfclib/algo/lizard/xx_lizard.h"
#include "xx_entropy.h"
#include "xxfclib/memory/xx_memory.h"

#include <stddef.h>
#include <stdint.h>

#define XX_LIZARD_MAGIC UINT32_C(0x184D2206)
#define XX_LIZARD_SKIP_MAGIC UINT32_C(0x184D2A50)

#define XX_LIZARD_STREAM_LITERALS UINT8_C(0x01)
#define XX_LIZARD_STREAM_FLAGS UINT8_C(0x02)
#define XX_LIZARD_STREAM_OFFSET16 UINT8_C(0x04)
#define XX_LIZARD_STREAM_OFFSET24 UINT8_C(0x08)
#define XX_LIZARD_STREAM_LENGTHS UINT8_C(0x10)
#define XX_LIZARD_RAW_SUBBLOCK UINT8_C(0x80)
#define XX_LIZARD_HUF_STREAM_LIMIT (128U * 1024U)

#define XX_LIZARD_XXH_P1 UINT32_C(2654435761)
#define XX_LIZARD_XXH_P2 UINT32_C(2246822519)
#define XX_LIZARD_XXH_P3 UINT32_C(3266489917)
#define XX_LIZARD_XXH_P4 UINT32_C(668265263)
#define XX_LIZARD_XXH_P5 UINT32_C(374761393)

typedef struct xx_lizard_streams {
    const uint8_t *flags;
    const uint8_t *flags_end;
    const uint8_t *offset16;
    const uint8_t *offset16_end;
    const uint8_t *offset24;
    const uint8_t *offset24_end;
    const uint8_t *literals;
    const uint8_t *literals_end;
} xx_lizard_streams;

static uint32_t xx_lizard_read24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16);
}

static uint32_t xx_lizard_read32(const uint8_t *p) {
    return xx_lizard_read24(p) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_lizard_read64(const uint8_t *p) {
    return (uint64_t)xx_lizard_read32(p) |
           ((uint64_t)xx_lizard_read32(p + 4) << 32);
}

static uint32_t xx_lizard_rotl(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32U - bits));
}

static uint32_t xx_lizard_xxh_round(uint32_t state, uint32_t word) {
    state += word * XX_LIZARD_XXH_P2;
    state = xx_lizard_rotl(state, 13);
    return state * XX_LIZARD_XXH_P1;
}

static uint32_t xx_lizard_xxh32(const uint8_t *data, size_t size) {
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint32_t hash;

    if (size >= 16U) {
        const uint8_t *limit = end - 16U;
        uint32_t a = XX_LIZARD_XXH_P1 + XX_LIZARD_XXH_P2;
        uint32_t b = XX_LIZARD_XXH_P2;
        uint32_t c = 0;
        uint32_t d = 0U - XX_LIZARD_XXH_P1;
        do {
            a = xx_lizard_xxh_round(a, xx_lizard_read32(cursor));
            b = xx_lizard_xxh_round(b, xx_lizard_read32(cursor + 4));
            c = xx_lizard_xxh_round(c, xx_lizard_read32(cursor + 8));
            d = xx_lizard_xxh_round(d, xx_lizard_read32(cursor + 12));
            cursor += 16;
        } while (cursor <= limit);
        hash = xx_lizard_rotl(a, 1) + xx_lizard_rotl(b, 7) +
               xx_lizard_rotl(c, 12) + xx_lizard_rotl(d, 18);
    } else {
        hash = XX_LIZARD_XXH_P5;
    }
    hash += (uint32_t)size;
    while ((size_t)(end - cursor) >= 4U) {
        hash += xx_lizard_read32(cursor) * XX_LIZARD_XXH_P3;
        hash = xx_lizard_rotl(hash, 17) * XX_LIZARD_XXH_P4;
        cursor += 4;
    }
    while (cursor < end) {
        hash += (uint32_t)(*cursor++) * XX_LIZARD_XXH_P5;
        hash = xx_lizard_rotl(hash, 11) * XX_LIZARD_XXH_P1;
    }
    hash ^= hash >> 15;
    hash *= XX_LIZARD_XXH_P2;
    hash ^= hash >> 13;
    hash *= XX_LIZARD_XXH_P3;
    return hash ^ (hash >> 16);
}

static bool xx_lizard_read_length(const uint8_t **cursor,
                                  const uint8_t *end, size_t base,
                                  size_t *result) {
    uint32_t extra;
    uint8_t tag;
    if (*cursor == end) return false;
    tag = *(*cursor)++;
    if (tag < UINT8_C(254)) {
        extra = tag;
    } else if (tag == UINT8_C(254)) {
        if ((size_t)(end - *cursor) < 2U) return false;
        extra = (uint32_t)(*cursor)[0] | ((uint32_t)(*cursor)[1] << 8);
        *cursor += 2;
    } else {
        if ((size_t)(end - *cursor) < 3U) return false;
        extra = xx_lizard_read24(*cursor);
        *cursor += 3;
    }
    if (base > SIZE_MAX - (size_t)extra) return false;
    *result = base + (size_t)extra;
    return true;
}

static bool xx_lizard_copy_match(uint8_t *destination,
                                 size_t destination_capacity,
                                 size_t *output_position, size_t distance,
                                 size_t length) {
    size_t output = *output_position;
    size_t match;
    if (distance == 0U || distance > output ||
        length > destination_capacity - output) {
        return false;
    }
    match = output - distance;
    for (size_t index = 0; index < length; ++index) {
        destination[output++] = destination[match++];
    }
    *output_position = output;
    return true;
}

static bool xx_lizard_copy_literals(xx_lizard_streams *streams,
                                    uint8_t *destination,
                                    size_t destination_capacity,
                                    size_t *output_position, size_t length) {
    size_t output = *output_position;
    if (length > (size_t)(streams->literals_end - streams->literals) ||
        length > destination_capacity - output) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        destination[output + index] = streams->literals[index];
    }
    streams->literals += length;
    *output_position = output + length;
    return true;
}

static bool xx_lizard_decode_lz4_streams(xx_lizard_streams *streams,
                                         uint8_t *destination,
                                         size_t destination_capacity,
                                         size_t *output_position) {
    while (streams->flags < streams->flags_end) {
        uint8_t token = *streams->flags++;
        size_t literal_length = (size_t)(token & 15U);
        size_t match_length = (size_t)(token >> 4);
        size_t distance;

        if (literal_length == 15U &&
            !xx_lizard_read_length(&streams->literals,
                                   streams->literals_end, 15U,
                                   &literal_length)) {
            return false;
        }
        if (!xx_lizard_copy_literals(streams, destination,
                                     destination_capacity, output_position,
                                     literal_length) ||
            (size_t)(streams->literals_end - streams->literals) < 2U) {
            return false;
        }
        distance = (size_t)streams->literals[0] |
                   ((size_t)streams->literals[1] << 8);
        streams->literals += 2;

        if (match_length == 15U &&
            !xx_lizard_read_length(&streams->literals,
                                   streams->literals_end, 15U,
                                   &match_length)) {
            return false;
        }
        if (match_length > SIZE_MAX - 4U ||
            !xx_lizard_copy_match(destination, destination_capacity,
                                  output_position, distance,
                                  match_length + 4U)) {
            return false;
        }
    }
    return xx_lizard_copy_literals(
               streams, destination, destination_capacity, output_position,
               (size_t)(streams->literals_end - streams->literals)) &&
           streams->offset16 == streams->offset16_end &&
           streams->offset24 == streams->offset24_end;
}

static bool xx_lizard_decode_v1_streams(xx_lizard_streams *streams,
                                        uint8_t *destination,
                                        size_t destination_capacity,
                                        size_t *output_position) {
    /* LIZv1 defines an implicit initial repeat distance of one byte for
       every independently encoded internal block. */
    size_t last_distance = 1U;

    while (streams->flags < streams->flags_end) {
        uint8_t token = *streams->flags++;
        size_t match_length;
        size_t distance;

        if (token >= 32U) {
            size_t literal_length = (size_t)(token & 7U);
            if (literal_length == 7U &&
                !xx_lizard_read_length(&streams->literals,
                                       streams->literals_end, 7U,
                                       &literal_length)) {
                return false;
            }
            if (!xx_lizard_copy_literals(streams, destination,
                                         destination_capacity,
                                         output_position, literal_length)) {
                return false;
            }

            if ((token & UINT8_C(0x80)) == 0U) {
                if ((size_t)(streams->offset16_end - streams->offset16) < 2U) {
                    return false;
                }
                last_distance = (size_t)streams->offset16[0] |
                                ((size_t)streams->offset16[1] << 8);
                streams->offset16 += 2;
            }
            distance = last_distance;
            match_length = (size_t)((token >> 3) & 15U);
            if (match_length == 15U &&
                !xx_lizard_read_length(&streams->literals,
                                       streams->literals_end, 15U,
                                       &match_length)) {
                return false;
            }
        } else {
            if ((size_t)(streams->offset24_end - streams->offset24) < 3U) {
                return false;
            }
            distance = xx_lizard_read24(streams->offset24);
            streams->offset24 += 3;
            last_distance = distance;
            if (token < 31U) {
                match_length = (size_t)token + 16U;
            } else if (!xx_lizard_read_length(
                           &streams->literals, streams->literals_end, 47U,
                           &match_length)) {
                return false;
            }
        }

        if (!xx_lizard_copy_match(destination, destination_capacity,
                                  output_position, distance, match_length)) {
            return false;
        }
    }

    return xx_lizard_copy_literals(
               streams, destination, destination_capacity, output_position,
               (size_t)(streams->literals_end - streams->literals)) &&
           streams->offset16 == streams->offset16_end &&
           streams->offset24 == streams->offset24_end;
}

static bool xx_lizard_plain_stream(const uint8_t **input,
                                   const uint8_t *input_end,
                                   const uint8_t **stream,
                                   const uint8_t **stream_end) {
    size_t length;
    if ((size_t)(input_end - *input) < 3U) return false;
    length = (size_t)xx_lizard_read24(*input);
    *input += 3;
    if (length > (size_t)(input_end - *input)) return false;
    *stream = *input;
    *stream_end = *input + length;
    *input += length;
    return true;
}

static bool xx_lizard_read_stream(const uint8_t **input,
                                  const uint8_t *input_end, bool compressed,
                                  uint8_t *scratch, size_t scratch_capacity,
                                  const uint8_t **stream,
                                  const uint8_t **stream_end) {
    if (!compressed) {
        return xx_lizard_plain_stream(input, input_end, stream, stream_end);
    }
    if ((size_t)(input_end - *input) < 6U) return false;
    {
        size_t decoded_size = (size_t)xx_lizard_read24(*input);
        size_t encoded_size = (size_t)xx_lizard_read24(*input + 3);
        *input += 6;
        if (!scratch || decoded_size == 0U ||
            decoded_size > scratch_capacity ||
            encoded_size > (size_t)(input_end - *input) ||
            !xx_huf_decompress(*input, encoded_size, scratch, decoded_size)) {
            return false;
        }
        *input += encoded_size;
        *stream = scratch;
        *stream_end = scratch + decoded_size;
        return true;
    }
}

static bool xx_lizard_decode_block(const uint8_t *source, size_t source_size,
                                   uint8_t *destination,
                                   size_t destination_capacity,
                                   size_t history_size,
                                   size_t *out_written) {
    const uint8_t *input = source;
    const uint8_t *end = source + source_size;
    size_t output = history_size;
    unsigned level;

    if (out_written) *out_written = 0;
    if (history_size > destination_capacity || input == end) return false;
    level = *input++;
    if (level < 10U || level > 49U) return false;

    while (input < end) {
        uint8_t control = *input++;
        xx_lizard_streams streams;
        const uint8_t *lengths;
        const uint8_t *lengths_end;
        uint8_t *scratch = NULL;
        bool parsed;
        bool decoded;

        if (control == XX_LIZARD_RAW_SUBBLOCK) {
            size_t length;
            if ((size_t)(end - input) < 3U) return false;
            length = (size_t)xx_lizard_read24(input);
            input += 3;
            if (length > (size_t)(end - input) ||
                length > destination_capacity - output) {
                return false;
            }
            for (size_t index = 0; index < length; ++index) {
                destination[output + index] = input[index];
            }
            input += length;
            output += length;
            continue;
        }
        if ((control & UINT8_C(0xE0)) != 0U ||
            (control & XX_LIZARD_STREAM_LENGTHS) != 0U) {
            return false;
        }

        if (!xx_lizard_plain_stream(&input, end, &lengths, &lengths_end)) {
            return false;
        }
        (void)lengths;
        (void)lengths_end;

        if ((control & (XX_LIZARD_STREAM_OFFSET16 |
                        XX_LIZARD_STREAM_OFFSET24 |
                        XX_LIZARD_STREAM_FLAGS |
                        XX_LIZARD_STREAM_LITERALS)) != 0U) {
            scratch = (uint8_t *)xx_mem_alloc(
                4U * XX_LIZARD_HUF_STREAM_LIMIT);
            if (!scratch) return false;
        }
        parsed = xx_lizard_read_stream(
                     &input, end,
                     (control & XX_LIZARD_STREAM_OFFSET16) != 0U,
                     scratch, XX_LIZARD_HUF_STREAM_LIMIT,
                     &streams.offset16, &streams.offset16_end) &&
                 xx_lizard_read_stream(
                     &input, end,
                     (control & XX_LIZARD_STREAM_OFFSET24) != 0U,
                     scratch ? scratch + XX_LIZARD_HUF_STREAM_LIMIT : NULL,
                     XX_LIZARD_HUF_STREAM_LIMIT,
                     &streams.offset24, &streams.offset24_end) &&
                 xx_lizard_read_stream(
                     &input, end,
                     (control & XX_LIZARD_STREAM_FLAGS) != 0U,
                     scratch ? scratch + 2U * XX_LIZARD_HUF_STREAM_LIMIT : NULL,
                     XX_LIZARD_HUF_STREAM_LIMIT,
                     &streams.flags, &streams.flags_end) &&
                 xx_lizard_read_stream(
                     &input, end,
                     (control & XX_LIZARD_STREAM_LITERALS) != 0U,
                     scratch ? scratch + 3U * XX_LIZARD_HUF_STREAM_LIMIT : NULL,
                     XX_LIZARD_HUF_STREAM_LIMIT,
                     &streams.literals, &streams.literals_end);
        decoded = parsed &&
            (((level >= 10U && level <= 19U) ||
              (level >= 30U && level <= 39U))
                 ? xx_lizard_decode_lz4_streams(
                       &streams, destination, destination_capacity, &output)
                 : xx_lizard_decode_v1_streams(
                       &streams, destination, destination_capacity, &output));
        if (scratch) xx_mem_free(scratch);
        if (!decoded) return false;
    }

    if (out_written) *out_written = output - history_size;
    return true;
}

static bool xx_lizard_block_limit(uint8_t descriptor, size_t *limit) {
    static const size_t limits[7] = {
        128U * 1024U, 256U * 1024U, 1024U * 1024U,
        4U * 1024U * 1024U, 16U * 1024U * 1024U,
        64U * 1024U * 1024U, 256U * 1024U * 1024U
    };
    unsigned id = (descriptor >> 4) & 7U;
    if (id == 0U || limits[id - 1U] > SIZE_MAX) return false;
    *limit = limits[id - 1U];
    return true;
}

bool xx_lizard_decompress_memory(const void *source, size_t source_size,
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
        magic = xx_lizard_read32(input);
        input += 4;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_LIZARD_SKIP_MAGIC) {
            uint32_t skip_size;
            if ((size_t)(end - input) < 4U) return false;
            skip_size = xx_lizard_read32(input);
            input += 4;
            if ((size_t)(end - input) < (size_t)skip_size) return false;
            input += skip_size;
            continue;
        }
        if (magic != XX_LIZARD_MAGIC || (size_t)(end - input) < 3U) return false;

        descriptor_start = input;
        flags = *input++;
        descriptor = *input++;
        if ((flags >> 6) != 1U || (flags & 3U) != 0U ||
            (descriptor & UINT8_C(0x8F)) != 0U ||
            !xx_lizard_block_limit(descriptor, &block_limit)) {
            return false;
        }
        independent = (flags & UINT8_C(0x20)) != 0U;
        block_checksum = (flags & UINT8_C(0x10)) != 0U;
        has_content_size = (flags & UINT8_C(0x08)) != 0U;
        content_checksum = (flags & UINT8_C(0x04)) != 0U;
        if (has_content_size) {
            if ((size_t)(end - input) < 8U) return false;
            content_size = xx_lizard_read64(input);
            input += 8;
        }
        if (input == end ||
            *input != (uint8_t)(xx_lizard_xxh32(
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
            stored_size = xx_lizard_read32(input);
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
                    xx_lizard_read32(input) !=
                        xx_lizard_xxh32(block_data, block_size)) {
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
                if (!xx_lizard_decode_block(
                        block_data, block_size, output + output_position,
                        destination_size - output_position, 0,
                        &block_output) || block_output > block_limit) {
                    return false;
                }
            } else {
                size_t history = output_position - frame_output_start;
                if (!xx_lizard_decode_block(
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
                xx_lizard_read32(input) != xx_lizard_xxh32(
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
