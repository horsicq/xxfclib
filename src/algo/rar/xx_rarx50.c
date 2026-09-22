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
 * Independent C11 implementation of the RAR 5.0 (Unpack50) bitstream.  The
 * implementation follows the public clean-room format description maintained
 * by the rar-research project.  RAR filters and malformed-stream behaviour were
 * cross-checked against the Apache-2.0 rars implementation.  No UnRAR-derived
 * source is used here.
 */

#include "xx_rarx_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>

#define XX_RARX50_LEVEL_SYMBOLS       20u
#define XX_RARX50_MAIN_SYMBOLS        306u
#define XX_RARX50_DISTANCE_SYMBOLS    64u
#define XX_RARX50_ALIGN_SYMBOLS       16u
#define XX_RARX50_LENGTH_SYMBOLS      44u
#define XX_RARX50_TOTAL_LENGTHS       (XX_RARX50_MAIN_SYMBOLS + \
                                       XX_RARX50_DISTANCE_SYMBOLS + \
                                       XX_RARX50_ALIGN_SYMBOLS + \
                                       XX_RARX50_LENGTH_SYMBOLS)
#define XX_RARX50_MAX_HUFF_SYMBOLS    XX_RARX50_MAIN_SYMBOLS
#define XX_RARX50_MAX_CODE_BITS       15u
#define XX_RARX50_MAX_FILTERS         8192u
#define XX_RARX50_MAX_FILTER_SIZE     (1u << 22)
#define XX_RARX50_X86_FILE_SIZE       0x01000000u
#define XX_RARX50_POLL_MASK           0x3fffu

typedef struct xx_rarx50_bits_s {
    const uint8_t *data;
    size_t bit_position;
    size_t bit_limit;
} xx_rarx50_bits;

typedef struct xx_rarx50_huffman_s {
    uint32_t first_code[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t first_index[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t count[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t symbols[XX_RARX50_MAX_HUFF_SYMBOLS];
    uint16_t symbol_count;
} xx_rarx50_huffman;

typedef enum xx_rarx50_filter_type_e {
    XX_RARX50_FILTER_DELTA = 0,
    XX_RARX50_FILTER_E8 = 1,
    XX_RARX50_FILTER_E8E9 = 2,
    XX_RARX50_FILTER_ARM = 3
} xx_rarx50_filter_type;

typedef struct xx_rarx50_filter_s {
    size_t start;
    size_t length;
    uint8_t channels;
    xx_rarx50_filter_type type;
} xx_rarx50_filter;

struct xx_rarx50_state {
    xx_rarx50_huffman main_table;
    xx_rarx50_huffman distance_table;
    xx_rarx50_huffman align_table;
    xx_rarx50_huffman length_table;
    bool tables_valid;
    bool align_mode;
    size_t repeated_distance[4];
    size_t last_length;
    uint8_t *history;
    size_t history_size;
    size_t history_capacity;
    size_t window_size;
};

static bool xx_rarx50_cancelled(xx_pd_struct *progress) {
    return progress && xx_pd_is_stopped(progress);
}

static uint32_t xx_rarx50_get_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void xx_rarx50_put_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static xx_rarx_status_t xx_rarx50_read_bits(xx_rarx50_bits *bits,
                                             unsigned count,
                                             uint32_t *value) {
    size_t end;
    uint32_t result = 0;
    unsigned remaining = count;

    if (!bits || !value || count > 32u ||
        bits->bit_position > bits->bit_limit ||
        count > bits->bit_limit - bits->bit_position) {
        return XX_RARX_STATUS_TRUNCATED;
    }
    end = bits->bit_position + count;
    while (bits->bit_position < end) {
        size_t byte_index = bits->bit_position >> 3;
        unsigned bit_offset = (unsigned)(bits->bit_position & 7u);
        unsigned available = 8u - bit_offset;
        unsigned take = remaining < available ? remaining : available;
        unsigned shift = available - take;
        uint32_t mask = (UINT32_C(1) << take) - 1u;
        uint32_t part = ((uint32_t)bits->data[byte_index] >> shift) & mask;
        result = (result << take) | part;
        bits->bit_position += take;
        remaining -= take;
    }
    *value = result;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_build_huffman(
    xx_rarx50_huffman *table, const uint8_t *lengths, size_t length_count) {
    uint16_t next_index[XX_RARX50_MAX_CODE_BITS + 1u];
    int32_t available = 1;
    uint32_t code = 0;
    size_t total = 0;
    size_t i;
    unsigned length;

    if (!table || !lengths || length_count > XX_RARX50_MAX_HUFF_SYMBOLS) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    xx_mem_zero(table, sizeof(*table));
    for (i = 0; i < length_count; ++i) {
        if (lengths[i] > XX_RARX50_MAX_CODE_BITS) {
            return XX_RARX_STATUS_CORRUPT;
        }
        if (lengths[i] != 0) {
            table->count[lengths[i]]++;
            total++;
        }
    }
    for (length = 1; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        available = available * 2 - (int32_t)table->count[length];
        if (available < 0) {
            return XX_RARX_STATUS_CORRUPT;
        }
    }
    if (total > UINT16_MAX) {
        return XX_RARX_STATUS_CORRUPT;
    }
    table->symbol_count = (uint16_t)total;

    total = 0;
    for (length = 1; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        code = (code + table->count[length - 1u]) << 1;
        table->first_code[length] = code;
        table->first_index[length] = (uint16_t)total;
        next_index[length] = (uint16_t)total;
        total += table->count[length];
    }
    for (i = 0; i < length_count; ++i) {
        uint8_t symbol_length = lengths[i];
        uint16_t index;
        if (symbol_length == 0) {
            continue;
        }
        index = next_index[symbol_length]++;
        if (index >= table->symbol_count) {
            return XX_RARX_STATUS_CORRUPT;
        }
        table->symbols[index] = (uint16_t)i;
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_decode_huffman(
    const xx_rarx50_huffman *table, xx_rarx50_bits *bits,
    unsigned *symbol) {
    uint32_t code = 0;
    unsigned length;

    if (!table || !bits || !symbol || table->symbol_count == 0) {
        return XX_RARX_STATUS_CORRUPT;
    }
    for (length = 1; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        uint32_t bit;
        uint32_t first;
        uint32_t offset;
        xx_rarx_status_t status = xx_rarx50_read_bits(bits, 1, &bit);
        if (status != XX_RARX_STATUS_OK) {
            return status;
        }
        code = (code << 1) | bit;
        first = table->first_code[length];
        if (code < first) {
            continue;
        }
        offset = code - first;
        if (offset < table->count[length]) {
            size_t index = (size_t)table->first_index[length] + offset;
            if (index >= table->symbol_count) {
                return XX_RARX_STATUS_CORRUPT;
            }
            *symbol = table->symbols[index];
            return XX_RARX_STATUS_OK;
        }
    }
    return XX_RARX_STATUS_CORRUPT;
}

static xx_rarx_status_t xx_rarx50_read_tables(xx_rarx50_state *state,
                                               xx_rarx50_bits *bits) {
    uint8_t level_lengths[XX_RARX50_LEVEL_SYMBOLS];
    uint8_t lengths[XX_RARX50_TOTAL_LENGTHS];
    xx_rarx50_huffman level_table;
    xx_rarx50_huffman main_table;
    xx_rarx50_huffman distance_table;
    xx_rarx50_huffman align_table;
    xx_rarx50_huffman length_table;
    size_t position = 0;
    size_t i;
    xx_rarx_status_t status;

    xx_mem_zero(level_lengths, sizeof(level_lengths));
    while (position < XX_RARX50_LEVEL_SYMBOLS) {
        uint32_t value;
        status = xx_rarx50_read_bits(bits, 4, &value);
        if (status != XX_RARX_STATUS_OK) return status;
        if (value == 15u) {
            uint32_t zero_count;
            status = xx_rarx50_read_bits(bits, 4, &zero_count);
            if (status != XX_RARX_STATUS_OK) return status;
            if (zero_count == 0) {
                level_lengths[position++] = 15;
            } else {
                size_t count = (size_t)zero_count + 2u;
                if (count > XX_RARX50_LEVEL_SYMBOLS - position) {
                    count = XX_RARX50_LEVEL_SYMBOLS - position;
                }
                position += count;
            }
        } else {
            level_lengths[position++] = (uint8_t)value;
        }
    }
    status = xx_rarx50_build_huffman(&level_table, level_lengths,
                                     XX_RARX50_LEVEL_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;

    position = 0;
    while (position < XX_RARX50_TOTAL_LENGTHS) {
        unsigned symbol;
        size_t count;
        uint32_t extra;
        status = xx_rarx50_decode_huffman(&level_table, bits, &symbol);
        if (status != XX_RARX_STATUS_OK) return status;
        if (symbol <= 15u) {
            lengths[position++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16u || symbol == 17u) {
            if (position == 0) return XX_RARX_STATUS_CORRUPT;
            status = xx_rarx50_read_bits(bits, symbol == 16u ? 3u : 7u,
                                         &extra);
            if (status != XX_RARX_STATUS_OK) return status;
            count = (symbol == 16u ? 3u : 11u) + (size_t)extra;
            if (count > XX_RARX50_TOTAL_LENGTHS - position) {
                count = XX_RARX50_TOTAL_LENGTHS - position;
            }
            for (i = 0; i < count; ++i) {
                lengths[position] = lengths[position - 1u];
                position++;
            }
            continue;
        }
        if (symbol == 18u || symbol == 19u) {
            status = xx_rarx50_read_bits(bits, symbol == 18u ? 3u : 7u,
                                         &extra);
            if (status != XX_RARX_STATUS_OK) return status;
            count = (symbol == 18u ? 3u : 11u) + (size_t)extra;
            if (count > XX_RARX50_TOTAL_LENGTHS - position) {
                count = XX_RARX50_TOTAL_LENGTHS - position;
            }
            xx_mem_zero(lengths + position, count);
            position += count;
            continue;
        }
        return XX_RARX_STATUS_CORRUPT;
    }

    status = xx_rarx50_build_huffman(&main_table, lengths,
                                     XX_RARX50_MAIN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(
        &distance_table, lengths + XX_RARX50_MAIN_SYMBOLS,
        XX_RARX50_DISTANCE_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(
        &align_table,
        lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS,
        XX_RARX50_ALIGN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(
        &length_table,
        lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS +
            XX_RARX50_ALIGN_SYMBOLS,
        XX_RARX50_LENGTH_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;

    state->main_table = main_table;
    state->distance_table = distance_table;
    state->align_table = align_table;
    state->length_table = length_table;
    state->align_mode = false;
    for (i = XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS;
         i < XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS +
                 XX_RARX50_ALIGN_SYMBOLS;
         ++i) {
        if (lengths[i] != 0 && lengths[i] != 4) {
            state->align_mode = true;
            break;
        }
    }
    state->tables_valid = true;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_read_filter_integer(
    xx_rarx50_bits *bits, uint32_t *value) {
    uint32_t width_code;
    uint32_t result = 0;
    unsigned i;
    xx_rarx_status_t status = xx_rarx50_read_bits(bits, 2, &width_code);
    if (status != XX_RARX_STATUS_OK) return status;
    for (i = 0; i <= width_code; ++i) {
        uint32_t byte_value;
        status = xx_rarx50_read_bits(bits, 8, &byte_value);
        if (status != XX_RARX_STATUS_OK) return status;
        result |= byte_value << (i * 8u);
    }
    *value = result;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_append_filter(
    xx_rarx50_filter **filters, size_t *filter_count, size_t current_position,
    size_t output_size, xx_rarx50_bits *bits) {
    xx_rarx50_filter filter;
    xx_rarx50_filter *resized;
    uint32_t offset;
    uint32_t length;
    uint32_t type;
    xx_rarx_status_t status;

    if (!filters || !filter_count || !bits) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    if (*filter_count >= XX_RARX50_MAX_FILTERS) {
        return XX_RARX_STATUS_LIMIT;
    }
    status = xx_rarx50_read_filter_integer(bits, &offset);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_read_filter_integer(bits, &length);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_read_bits(bits, 3, &type);
    if (status != XX_RARX_STATUS_OK) return status;
    if (type > XX_RARX50_FILTER_ARM) {
        return XX_RARX_STATUS_UNSUPPORTED_FILTER;
    }
    if (length == 0 || length > XX_RARX50_MAX_FILTER_SIZE) {
        return XX_RARX_STATUS_LIMIT;
    }
    if ((size_t)offset > SIZE_MAX - current_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    filter.start = current_position + (size_t)offset;
    filter.length = (size_t)length;
    if (filter.start > output_size ||
        filter.length > output_size - filter.start) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (*filter_count != 0) {
        const xx_rarx50_filter *previous = &(*filters)[*filter_count - 1u];
        if (previous->length > SIZE_MAX - previous->start ||
            filter.start < previous->start + previous->length) {
            return XX_RARX_STATUS_CORRUPT;
        }
    }
    filter.type = (xx_rarx50_filter_type)type;
    filter.channels = 0;
    if (filter.type == XX_RARX50_FILTER_DELTA) {
        uint32_t channels;
        status = xx_rarx50_read_bits(bits, 5, &channels);
        if (status != XX_RARX_STATUS_OK) return status;
        filter.channels = (uint8_t)(channels + 1u);
    }

    if (*filter_count == SIZE_MAX / sizeof(**filters)) {
        return XX_RARX_STATUS_LIMIT;
    }
    resized = (xx_rarx50_filter *)xx_rarx_clear_resize(
        *filters, *filter_count * sizeof(**filters),
        (*filter_count + 1u) * sizeof(**filters));
    if (!resized) {
        return XX_RARX_STATUS_NO_MEMORY;
    }
    resized[*filter_count] = filter;
    *filters = resized;
    (*filter_count)++;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_length_from_slot(
    unsigned slot, xx_rarx50_bits *bits, size_t *length) {
    unsigned bit_count = 0;
    uint32_t extra = 0;
    size_t base;
    xx_rarx_status_t status;

    if (slot >= XX_RARX50_LENGTH_SYMBOLS) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (slot < 8u) {
        *length = (size_t)slot + 2u;
        return XX_RARX_STATUS_OK;
    }
    bit_count = (slot >> 2) - 1u;
    if (bit_count > 24u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    status = xx_rarx50_read_bits(bits, bit_count, &extra);
    if (status != XX_RARX_STATUS_OK) return status;
    base = (size_t)(4u | (slot & 3u)) << bit_count;
    if (base > SIZE_MAX - (size_t)extra - 2u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    *length = base + (size_t)extra + 2u;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_distance_from_slot(
    unsigned slot, const xx_rarx50_huffman *align_table, bool align_mode,
    xx_rarx50_bits *bits, size_t *distance) {
    unsigned bit_count;
    uint32_t extra = 0;
    size_t base;
    xx_rarx_status_t status;

    if (slot >= XX_RARX50_DISTANCE_SYMBOLS) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (slot < 4u) {
        *distance = (size_t)slot + 1u;
        return XX_RARX_STATUS_OK;
    }
    bit_count = (slot - 2u) >> 1;
    if (bit_count > 31u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (align_mode && bit_count >= 4u) {
        uint32_t high;
        unsigned low;
        status = xx_rarx50_read_bits(bits, bit_count - 4u, &high);
        if (status != XX_RARX_STATUS_OK) return status;
        status = xx_rarx50_decode_huffman(align_table, bits, &low);
        if (status != XX_RARX_STATUS_OK) return status;
        if (low >= XX_RARX50_ALIGN_SYMBOLS) {
            return XX_RARX_STATUS_CORRUPT;
        }
        extra = (high << 4) | (uint32_t)low;
    } else {
        status = xx_rarx50_read_bits(bits, bit_count, &extra);
        if (status != XX_RARX_STATUS_OK) return status;
    }
    base = (size_t)(2u | (slot & 1u)) << bit_count;
    if (base > SIZE_MAX - (size_t)extra - 1u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    *distance = base + (size_t)extra + 1u;
    return XX_RARX_STATUS_OK;
}

static size_t xx_rarx50_length_bonus(size_t distance) {
    size_t bonus = 0;
    if (distance > 0x100u) bonus++;
    if (distance > 0x2000u) bonus++;
    if (distance > 0x40000u) bonus++;
    return bonus;
}

static xx_rarx_status_t xx_rarx50_copy_match(
    const xx_rarx50_state *state, uint8_t *destination,
    size_t *destination_position, size_t destination_size,
    size_t window_size, size_t distance, size_t length) {
    size_t output_position;
    size_t available;
    size_t i;

    if (!state || !destination_position ||
        *destination_position > destination_size ||
        length > destination_size - *destination_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    output_position = *destination_position;
    if (state->history_size > SIZE_MAX - output_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    available = state->history_size + output_position;

    /*
     * RAR5's initialized window reads as zero before enough history exists.
     * Preserve that compatibility rule for a syntactically valid match which
     * reaches outside the available or declared window.
     */
    if (distance == 0 || distance > window_size || distance > available) {
        if (length != 0) {
            xx_mem_zero(destination + output_position, length);
        }
        *destination_position += length;
        return XX_RARX_STATUS_OK;
    }

    for (i = 0; i < length; ++i) {
        size_t absolute_position = state->history_size + output_position + i;
        size_t source_position = absolute_position - distance;
        uint8_t value;
        if (source_position < state->history_size) {
            value = state->history[source_position];
        } else {
            value = destination[source_position - state->history_size];
        }
        destination[output_position + i] = value;
    }
    *destination_position += length;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_remember_history(
    xx_rarx50_state *state, const uint8_t *data, size_t data_size,
    size_t window_size) {
    size_t old_keep;
    size_t data_keep;
    size_t wanted;
    uint8_t *resized;

    if (!state || (!data && data_size != 0) || window_size == 0) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    data_keep = data_size < window_size ? data_size : window_size;
    old_keep = window_size - data_keep;
    if (old_keep > state->history_size) old_keep = state->history_size;
    wanted = old_keep + data_keep;
    if (wanted > state->history_capacity) {
        resized = (uint8_t *)xx_rarx_clear_resize(state->history,
                            state->history_capacity, wanted);
        if (!resized && wanted != 0) {
            return XX_RARX_STATUS_NO_MEMORY;
        }
        state->history = resized;
        state->history_capacity = wanted;
    }
    if (old_keep != 0 && old_keep != state->history_size) {
        xx_mem_move(state->history,
                    state->history + state->history_size - old_keep,
                    old_keep);
    }
    if (data_keep != 0) {
        xx_mem_copy(state->history + old_keep,
                    data + data_size - data_keep, data_keep);
    }
    state->history_size = wanted;
    if (wanted < state->history_capacity)
        xx_mem_zero(state->history + wanted, state->history_capacity - wanted);
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_delta(
    uint8_t *data, size_t size, unsigned channels, xx_pd_struct *progress) {
    uint8_t *decoded;
    size_t source_position = 0;
    unsigned channel;

    if (!data || channels == 0 || channels > 32u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    decoded = (uint8_t *)xx_mem_alloc(size != 0 ? size : 1u);
    if (!decoded) return XX_RARX_STATUS_NO_MEMORY;
    for (channel = 0; channel < channels; ++channel) {
        uint8_t previous = 0;
        size_t position;
        for (position = channel; position < size; position += channels) {
            if ((source_position & XX_RARX50_POLL_MASK) == 0 &&
                xx_rarx50_cancelled(progress)) {
                xx_rarx_clear_free(decoded, size != 0 ? size : 1u);
                return XX_RARX_STATUS_CANCELLED;
            }
            if (source_position >= size) {
                xx_rarx_clear_free(decoded, size != 0 ? size : 1u);
                return XX_RARX_STATUS_CORRUPT;
            }
            previous = (uint8_t)(previous - data[source_position++]);
            decoded[position] = previous;
        }
    }
    if (source_position != size) {
        xx_rarx_clear_free(decoded, size != 0 ? size : 1u);
        return XX_RARX_STATUS_CORRUPT;
    }
    xx_mem_copy(data, decoded, size);
    xx_rarx_clear_free(decoded, size != 0 ? size : 1u);
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_x86(
    uint8_t *data, size_t size, uint32_t file_offset, bool include_e9,
    xx_pd_struct *progress) {
    size_t position = 0;

    while (position + 4u < size) {
        uint8_t opcode;
        if ((position & XX_RARX50_POLL_MASK) == 0 &&
            xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        opcode = data[position];
        if (opcode == 0xe8u || (include_e9 && opcode == 0xe9u)) {
            size_t address_position = position + 1u;
            uint32_t offset = (file_offset + (uint32_t)address_position) &
                              (XX_RARX50_X86_FILE_SIZE - 1u);
            uint32_t address = xx_rarx50_get_le32(data + address_position);
            if (address < XX_RARX50_X86_FILE_SIZE) {
                address -= offset;
                xx_rarx50_put_le32(data + address_position, address);
            } else if (address > UINT32_MAX - offset) {
                address += XX_RARX50_X86_FILE_SIZE;
                xx_rarx50_put_le32(data + address_position, address);
            }
            position += 5u;
        } else {
            position++;
        }
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_arm(
    uint8_t *data, size_t size, uint32_t file_offset,
    xx_pd_struct *progress) {
    size_t position;
    for (position = 0; position + 3u < size; position += 4u) {
        uint32_t value;
        uint32_t offset;
        if ((position & XX_RARX50_POLL_MASK) == 0 &&
            xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        if (data[position + 3u] != 0xebu) continue;
        value = (uint32_t)data[position] |
                ((uint32_t)data[position + 1u] << 8) |
                ((uint32_t)data[position + 2u] << 16);
        offset = (file_offset + (uint32_t)position) >> 2;
        value = (value - offset) & 0x00ffffffu;
        data[position] = (uint8_t)value;
        data[position + 1u] = (uint8_t)(value >> 8);
        data[position + 2u] = (uint8_t)(value >> 16);
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_apply_filters(
    uint8_t *destination, size_t destination_size,
    const xx_rarx50_filter *filters, size_t filter_count,
    xx_pd_struct *progress) {
    size_t i;
    for (i = 0; i < filter_count; ++i) {
        const xx_rarx50_filter *filter = &filters[i];
        uint8_t *data;
        xx_rarx_status_t status;
        if (xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        if (filter->start > destination_size ||
            filter->length > destination_size - filter->start) {
            return XX_RARX_STATUS_CORRUPT;
        }
        data = destination + filter->start;
        switch (filter->type) {
            case XX_RARX50_FILTER_DELTA:
                status = xx_rarx50_filter_delta(data, filter->length,
                                                filter->channels, progress);
                break;
            case XX_RARX50_FILTER_E8:
                status = xx_rarx50_filter_x86(
                    data, filter->length, (uint32_t)filter->start, false,
                    progress);
                break;
            case XX_RARX50_FILTER_E8E9:
                status = xx_rarx50_filter_x86(
                    data, filter->length, (uint32_t)filter->start, true,
                    progress);
                break;
            case XX_RARX50_FILTER_ARM:
                status = xx_rarx50_filter_arm(
                    data, filter->length, (uint32_t)filter->start, progress);
                break;
            default:
                status = XX_RARX_STATUS_UNSUPPORTED_FILTER;
                break;
        }
        if (status != XX_RARX_STATUS_OK) return status;
    }
    return XX_RARX_STATUS_OK;
}

xx_rarx50_state *xx_rarx50_create(void) {
    return (xx_rarx50_state *)xx_mem_calloc(1, sizeof(xx_rarx50_state));
}

void xx_rarx50_destroy(xx_rarx50_state *state) {
    if (!state) return;
    xx_rarx_clear_free(state->history, state->history_capacity);
    xx_rarx_clear_free(state, sizeof(*state));
}

void xx_rarx50_reset(xx_rarx50_state *state) {
    uint8_t *history;
    size_t capacity;
    if (!state) return;
    history = state->history;
    capacity = state->history_capacity;
    xx_mem_zero(history, capacity);
    xx_mem_zero(state, sizeof(*state));
    state->history = history;
    state->history_capacity = capacity;
}

xx_rarx_status_t xx_rarx50_decode(xx_rarx50_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination,
                                  size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used,
                                  xx_pd_struct *progress) {
    xx_rarx50_filter *filters = NULL;
    size_t filter_count = 0;
    size_t source_position = 0;
    size_t destination_position = 0;
    size_t block_count = 0;
    bool saw_last_block = false;
    xx_rarx_status_t status = XX_RARX_STATUS_OK;

    if (source_used) *source_used = 0;
    if (!state || (!source && source_size != 0) ||
        (!destination && destination_size != 0) || window_size == 0) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    if (xx_rarx50_cancelled(progress)) {
        return XX_RARX_STATUS_CANCELLED;
    }
    if (!solid) {
        xx_rarx50_reset(state);
        state->window_size = window_size;
    } else if (state->window_size == 0 ||
               state->window_size != window_size) {
        return XX_RARX_STATUS_CORRUPT;
    }

    while (!saw_last_block) {
        uint8_t flags;
        uint8_t checksum;
        unsigned size_width_code;
        size_t size_width;
        size_t header_size;
        size_t payload_size = 0;
        size_t payload_bits;
        size_t i;
        xx_rarx50_bits bits;

        if (xx_rarx50_cancelled(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
            goto cleanup;
        }
        if (source_position > source_size ||
            source_size - source_position < 3u) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        flags = source[source_position];
        checksum = source[source_position + 1u];
        size_width_code = (unsigned)((flags >> 3) & 3u);
        if (size_width_code == 3u) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        size_width = (size_t)size_width_code + 1u;
        header_size = 2u + size_width;
        if (header_size > source_size - source_position) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        for (i = 0; i < size_width; ++i) {
            uint8_t byte_value = source[source_position + 2u + i];
            checksum ^= byte_value;
            payload_size |= (size_t)byte_value << (i * 8u);
        }
        checksum ^= flags;
        if (checksum != 0x5au) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        if (payload_size > source_size - source_position - header_size) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        if (payload_size == 0) {
            payload_bits = 0;
        } else {
            unsigned final_bits = (unsigned)(flags & 7u) + 1u;
            payload_bits = (payload_size - 1u) * 8u + final_bits;
        }
        bits.data = source + source_position + header_size;
        bits.bit_position = 0;
        bits.bit_limit = payload_bits;
        source_position += header_size + payload_size;
        block_count++;

        if ((flags & 0x80u) != 0) {
            status = xx_rarx50_read_tables(state, &bits);
            if (status != XX_RARX_STATUS_OK) goto cleanup;
        } else if (!state->tables_valid) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }

        while (bits.bit_position < bits.bit_limit) {
            unsigned symbol;
            if (((destination_position + bits.bit_position) &
                 XX_RARX50_POLL_MASK) == 0 &&
                xx_rarx50_cancelled(progress)) {
                status = XX_RARX_STATUS_CANCELLED;
                goto cleanup;
            }
            status = xx_rarx50_decode_huffman(&state->main_table, &bits,
                                              &symbol);
            if (status != XX_RARX_STATUS_OK) goto cleanup;
            if (symbol < 256u) {
                if (destination_position >= destination_size) {
                    status = XX_RARX_STATUS_CORRUPT;
                    goto cleanup;
                }
                destination[destination_position++] = (uint8_t)symbol;
            } else if (symbol == 256u) {
                status = xx_rarx50_append_filter(
                    &filters, &filter_count, destination_position,
                    destination_size, &bits);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
            } else if (symbol == 257u) {
                if (state->last_length != 0) {
                    status = xx_rarx50_copy_match(
                        state, destination, &destination_position,
                        destination_size, window_size,
                        state->repeated_distance[0], state->last_length);
                    if (status != XX_RARX_STATUS_OK) goto cleanup;
                }
            } else if (symbol <= 261u) {
                unsigned repeated_index = symbol - 258u;
                size_t distance = state->repeated_distance[repeated_index];
                size_t length;
                unsigned length_slot;
                unsigned index;
                if (distance == 0) {
                    status = XX_RARX_STATUS_CORRUPT;
                    goto cleanup;
                }
                status = xx_rarx50_decode_huffman(
                    &state->length_table, &bits, &length_slot);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                status = xx_rarx50_length_from_slot(length_slot, &bits,
                                                    &length);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                for (index = repeated_index; index != 0; --index) {
                    state->repeated_distance[index] =
                        state->repeated_distance[index - 1u];
                }
                state->repeated_distance[0] = distance;
                state->last_length = length;
                status = xx_rarx50_copy_match(
                    state, destination, &destination_position,
                    destination_size, window_size, distance, length);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
            } else {
                unsigned length_slot = symbol - 262u;
                unsigned distance_slot;
                size_t distance;
                size_t length;
                size_t bonus;
                status = xx_rarx50_length_from_slot(length_slot, &bits,
                                                    &length);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                status = xx_rarx50_decode_huffman(
                    &state->distance_table, &bits, &distance_slot);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                status = xx_rarx50_distance_from_slot(
                    distance_slot, &state->align_table, state->align_mode,
                    &bits, &distance);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                bonus = xx_rarx50_length_bonus(distance);
                if (length > SIZE_MAX - bonus) {
                    status = XX_RARX_STATUS_CORRUPT;
                    goto cleanup;
                }
                length += bonus;
                state->repeated_distance[3] =
                    state->repeated_distance[2];
                state->repeated_distance[2] =
                    state->repeated_distance[1];
                state->repeated_distance[1] =
                    state->repeated_distance[0];
                state->repeated_distance[0] = distance;
                state->last_length = length;
                status = xx_rarx50_copy_match(
                    state, destination, &destination_position,
                    destination_size, window_size, distance, length);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
            }
        }
        if (bits.bit_position != bits.bit_limit) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        saw_last_block = (flags & 0x40u) != 0;
        if (!saw_last_block && destination_position >= destination_size) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        if (!saw_last_block && source_position >= source_size) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        if (block_count > source_size / 3u + 1u) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
    }

    if (destination_position != destination_size) {
        status = XX_RARX_STATUS_TRUNCATED;
        goto cleanup;
    }
    status = xx_rarx50_remember_history(state, destination,
                                        destination_size, window_size);
    if (status != XX_RARX_STATUS_OK) goto cleanup;
    status = xx_rarx50_apply_filters(destination, destination_size, filters,
                                     filter_count, progress);
    if (status != XX_RARX_STATUS_OK) goto cleanup;
    if (source_used) *source_used = source_position;

cleanup:
    xx_rarx_clear_free(filters, filter_count * sizeof(*filters));
    return status;
}
