/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Oracle Squeeze uses a signed-child Huffman tree and the traditional 0x90
 * repeat escape.  Bits are consumed low bit first from each payload byte.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/oraclesqueeze/xx_oraclesqueeze.h"

#include <string.h>

#define XX_ORACLESQUEEZE_MAX_NODES 256U
#define XX_ORACLESQUEEZE_EOF 256U
#define XX_ORACLESQUEEZE_RLE_ESCAPE 0x90U

typedef struct xx_oraclesqueeze_bit_reader_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    unsigned bits_left;
} xx_oraclesqueeze_bit_reader;

static uint16_t xx_oraclesqueeze_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static int32_t xx_oraclesqueeze_read_signed16le(const uint8_t *data) {
    uint16_t value = xx_oraclesqueeze_read16le(data);
    return (value & UINT16_C(0x8000)) != 0U ?
               (int32_t)value - INT32_C(65536) : (int32_t)value;
}

bool xx_oraclesqueeze_parse_tree(const uint8_t *input, size_t input_size,
                                 xx_oraclesqueeze_tree_info *info) {
    uint16_t node_count;
    size_t index;
    size_t table_size;
    if (!input || !info || input_size < 7U) return false;
    node_count = xx_oraclesqueeze_read16le(input);
    if (node_count == 0U || node_count > XX_ORACLESQUEEZE_MAX_NODES ||
        (size_t)node_count > (SIZE_MAX - 2U) / 4U) {
        return false;
    }
    table_size = 2U + (size_t)node_count * 4U;
    if (table_size >= input_size) return false;
    for (index = 0U; index < (size_t)node_count * 2U; ++index) {
        int32_t child = xx_oraclesqueeze_read_signed16le(input + 2U +
                                                         index * 2U);
        if (child >= (int32_t)node_count ||
            (child < 0 && (uint32_t)(-child - 1) > XX_ORACLESQUEEZE_EOF)) {
            return false;
        }
    }
    info->node_count = node_count;
    info->bitstream_offset = table_size;
    return true;
}

static bool xx_oraclesqueeze_read_bit(xx_oraclesqueeze_bit_reader *reader,
                                      uint32_t *bit) {
    if (!reader || !bit) return false;
    if (reader->bits_left == 0U) {
        if (reader->position >= reader->size) return false;
        reader->current = reader->data[reader->position++];
        reader->bits_left = 8U;
    }
    *bit = (uint32_t)(reader->current & 1U);
    reader->current >>= 1U;
    --reader->bits_left;
    return true;
}

static bool xx_oraclesqueeze_decode_symbol(
    xx_oraclesqueeze_bit_reader *reader, const uint8_t *tree,
    uint16_t node_count, uint32_t *symbol) {
    uint16_t node = 0U;
    unsigned guard = 0U;
    if (!reader || !tree || node_count == 0U || !symbol) return false;
    for (;;) {
        uint32_t bit;
        int32_t child;
        if (++guard > node_count || !xx_oraclesqueeze_read_bit(reader, &bit)) {
            return false;
        }
        child = xx_oraclesqueeze_read_signed16le(tree +
                                                   (size_t)node * 4U +
                                                   (size_t)bit * 2U);
        if (child < 0) {
            *symbol = (uint32_t)(-child - 1);
            return true;
        }
        if ((uint32_t)child >= node_count) return false;
        node = (uint16_t)child;
    }
}

static bool xx_oraclesqueeze_emit(uint8_t *output, size_t output_size,
                                  size_t *position, uint8_t value,
                                  uint16_t *checksum) {
    if (!output || !position || !checksum || *position >= output_size) {
        return false;
    }
    output[(*position)++] = value;
    *checksum = (uint16_t)(*checksum + value);
    return true;
}

bool xx_oraclesqueeze_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, uint16_t *checksum) {
    xx_oraclesqueeze_tree_info tree_info;
    xx_oraclesqueeze_bit_reader reader;
    size_t output_position = 0U;
    uint16_t calculated_checksum = 0U;
    bool repeat_pending = false;
    bool has_last = false;
    uint8_t last = 0U;
    if (consumed_size) *consumed_size = 0U;
    if (checksum) *checksum = 0U;
    if (!input || !output || output_size == 0U ||
        !xx_oraclesqueeze_parse_tree(input, input_size, &tree_info)) {
        return false;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input + tree_info.bitstream_offset;
    reader.size = input_size - tree_info.bitstream_offset;
    for (;;) {
        uint32_t symbol;
        if (output_position == output_size && !repeat_pending) break;
        if (!xx_oraclesqueeze_decode_symbol(&reader, input + 2U,
                                            tree_info.node_count, &symbol)) {
            return false;
        }
        if (symbol == XX_ORACLESQUEEZE_EOF) {
            if (repeat_pending || output_position != output_size) return false;
            break;
        }
        if (repeat_pending) {
            uint32_t count;
            repeat_pending = false;
            if (symbol == 0U) {
                if (!xx_oraclesqueeze_emit(output, output_size,
                                            &output_position,
                                            XX_ORACLESQUEEZE_RLE_ESCAPE,
                                            &calculated_checksum)) {
                    return false;
                }
                last = XX_ORACLESQUEEZE_RLE_ESCAPE;
                has_last = true;
                continue;
            }
            if (!has_last) return false;
            for (count = 1U; count < symbol; ++count) {
                if (!xx_oraclesqueeze_emit(output, output_size,
                                            &output_position, last,
                                            &calculated_checksum)) {
                    return false;
                }
            }
            continue;
        }
        if (symbol == XX_ORACLESQUEEZE_RLE_ESCAPE) {
            repeat_pending = true;
            continue;
        }
        if (!xx_oraclesqueeze_emit(output, output_size, &output_position,
                                    (uint8_t)symbol,
                                    &calculated_checksum)) {
            return false;
        }
        last = (uint8_t)symbol;
        has_last = true;
    }
    if (output_position != output_size || repeat_pending) return false;
    if (consumed_size) {
        *consumed_size = tree_info.bitstream_offset + reader.position;
    }
    if (checksum) *checksum = calculated_checksum;
    return true;
}
