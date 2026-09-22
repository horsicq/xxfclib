/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native bounds-checked decoder for Nintendo's LZ10 and LZ11 packed streams.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/wiilz77/xx_wiilz77.h"

#include <string.h>

#define XX_WIILZ77_TAG_SIZE 4U
#define XX_WIILZ77_SHORT_HEADER_SIZE 4U
#define XX_WIILZ77_EXTENDED_HEADER_SIZE 8U

static uint32_t xx_wiilz77_read24le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U);
}

static uint32_t xx_wiilz77_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

bool xx_wiilz77_parse_header(const uint8_t *input, size_t input_size,
                             xx_wiilz77_header *header) {
    size_t offset = 0U;
    uint32_t size24;
    uint64_t uncompressed_size;
    if (!input || !header) return false;
    xx_rt_memset(header, 0, sizeof(*header));
    if (input_size >= XX_WIILZ77_TAG_SIZE &&
        xx_rt_memcmp(input, "LZ77", XX_WIILZ77_TAG_SIZE) == 0) {
        offset = XX_WIILZ77_TAG_SIZE;
        header->has_tag = true;
    }
    if (input_size - offset < XX_WIILZ77_SHORT_HEADER_SIZE) return false;
    if (input[offset] == (uint8_t)XX_WIILZ77_VARIANT_LZ10) {
        header->variant = XX_WIILZ77_VARIANT_LZ10;
    } else if (input[offset] == (uint8_t)XX_WIILZ77_VARIANT_LZ11) {
        header->variant = XX_WIILZ77_VARIANT_LZ11;
    } else {
        return false;
    }
    size24 = xx_wiilz77_read24le(input + offset + 1U);
    if (size24 != 0U) {
        uncompressed_size = size24;
        header->header_size = offset + XX_WIILZ77_SHORT_HEADER_SIZE;
    } else {
        if (input_size - offset < XX_WIILZ77_EXTENDED_HEADER_SIZE) {
            return false;
        }
        uncompressed_size = xx_wiilz77_read32le(input + offset + 4U);
        header->header_size = offset + XX_WIILZ77_EXTENDED_HEADER_SIZE;
    }
    if (uncompressed_size == 0U) return false;
    header->uncompressed_size = uncompressed_size;
    return true;
}

static bool xx_wiilz77_copy_match(uint8_t *output, size_t output_size,
                                   size_t *output_position, size_t distance,
                                   size_t length) {
    size_t index;
    if (!output || !output_position || distance == 0U ||
        distance > *output_position || length > output_size - *output_position) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        output[*output_position] = output[*output_position - distance];
        ++*output_position;
    }
    return true;
}

bool xx_wiilz77_decompress_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *consumed_size) {
    xx_wiilz77_header header;
    size_t input_position;
    size_t output_position = 0U;
    if (consumed_size) *consumed_size = 0U;
    if (!input || !output ||
        !xx_wiilz77_parse_header(input, input_size, &header) ||
        header.uncompressed_size > (uint64_t)SIZE_MAX ||
        output_size != (size_t)header.uncompressed_size) {
        return false;
    }
    input_position = header.header_size;
    while (output_position < output_size) {
        uint8_t flags;
        uint8_t mask;
        if (input_position >= input_size) return false;
        flags = input[input_position++];
        for (mask = 0x80U; mask != 0U && output_position < output_size;
             mask >>= 1U) {
            if ((flags & mask) == 0U) {
                if (input_position >= input_size) return false;
                output[output_position++] = input[input_position++];
            } else {
                uint8_t first;
                uint8_t second;
                size_t length;
                size_t distance;
                if (input_position >= input_size) return false;
                first = input[input_position++];
                if (header.variant == XX_WIILZ77_VARIANT_LZ10) {
                    if (input_position >= input_size) return false;
                    second = input[input_position++];
                    length = (size_t)(first >> 4U) + 3U;
                    distance = ((size_t)(first & 0x0fU) << 8U) |
                               (size_t)second;
                    ++distance;
                } else {
                    uint8_t indicator = (uint8_t)(first >> 4U);
                    if (input_position >= input_size) return false;
                    second = input[input_position++];
                    if (indicator == 0U) {
                        uint8_t third;
                        if (input_position >= input_size) return false;
                        third = input[input_position++];
                        length = ((size_t)(first & 0x0fU) << 4U) |
                                 (size_t)(second >> 4U);
                        length += 0x11U;
                        distance = ((size_t)(second & 0x0fU) << 8U) |
                                   (size_t)third;
                        ++distance;
                    } else if (indicator == 1U) {
                        uint8_t third;
                        uint8_t fourth;
                        if (input_size - input_position < 2U) return false;
                        third = input[input_position++];
                        fourth = input[input_position++];
                        length = ((size_t)(first & 0x0fU) << 12U) |
                                 ((size_t)second << 4U) |
                                 (size_t)(third >> 4U);
                        length += 0x111U;
                        distance = ((size_t)(third & 0x0fU) << 8U) |
                                   (size_t)fourth;
                        ++distance;
                    } else {
                        length = (size_t)indicator + 1U;
                        distance = ((size_t)(first & 0x0fU) << 8U) |
                                   (size_t)second;
                        ++distance;
                    }
                }
                if (!xx_wiilz77_copy_match(output, output_size,
                                            &output_position, distance,
                                            length)) {
                    return false;
                }
            }
        }
    }
    if (consumed_size) *consumed_size = input_position;
    return true;
}
