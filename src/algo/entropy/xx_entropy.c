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

/* Independent finite-state and canonical Huffman decoder primitives. */

#include "xx_entropy.h"
#include "platforms/xx_entropy_platform.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"

#include <stddef.h>
#include <stdint.h>

typedef struct xx_forward_bits {
    const uint8_t *data;
    size_t size;
    size_t bit_position;
} xx_forward_bits;

static unsigned xx_high_bit32(uint32_t value) {
    unsigned result = 0;
    while (value > 1U) {
        value >>= 1;
        ++result;
    }
    return result;
}

bool xx_reverse_bits_init(xx_reverse_bits *bits, const void *source,
                          size_t source_size) {
    const uint8_t *bytes = (const uint8_t *)source;
    uint8_t final_byte;
    unsigned marker;
    if (!bits || !bytes || source_size == 0U) return false;
    final_byte = bytes[source_size - 1U];
    if (final_byte == 0U) return false;
    marker = xx_high_bit32(final_byte);
    if (source_size - 1U > (SIZE_MAX - marker) / 8U) return false;
    bits->data = bytes;
    bits->bit_position = (source_size - 1U) * 8U + marker;
    return true;
}

bool xx_reverse_bits_peek_padded(const xx_reverse_bits *bits, unsigned count,
                                 uint32_t *value) {
    size_t available;
    uint32_t result = 0;
    if (!bits || !bits->data || !value || count > 24U) return false;
    available = bits->bit_position < (size_t)count
                    ? bits->bit_position : (size_t)count;
    for (size_t index = 0; index < available; ++index) {
        size_t position = bits->bit_position - index - 1U;
        result = (result << 1) |
                 ((uint32_t)(bits->data[position >> 3] >> (position & 7U)) & 1U);
    }
    result <<= (unsigned)((size_t)count - available);
    *value = result;
    return true;
}

bool xx_reverse_bits_skip(xx_reverse_bits *bits, unsigned count) {
    if (!bits || (size_t)count > bits->bit_position) return false;
    bits->bit_position -= count;
    return true;
}

bool xx_reverse_bits_read(xx_reverse_bits *bits, unsigned count,
                          uint32_t *value) {
    if (!xx_reverse_bits_peek_padded(bits, count, value) ||
        (size_t)count > bits->bit_position) {
        return false;
    }
    bits->bit_position -= count;
    return true;
}

static bool xx_forward_peek(const xx_forward_bits *bits, unsigned count,
                            uint32_t *value) {
    uint32_t result = 0;
    if (!bits || !value || count > 24U ||
        bits->bit_position > bits->size * 8U ||
        (size_t)count > bits->size * 8U - bits->bit_position) {
        return false;
    }
    for (unsigned index = 0; index < count; ++index) {
        size_t position = bits->bit_position + index;
        result |= ((uint32_t)(bits->data[position >> 3] >> (position & 7U)) & 1U)
                  << index;
    }
    *value = result;
    return true;
}

static bool xx_forward_read(xx_forward_bits *bits, unsigned count,
                            uint32_t *value) {
    if (!xx_forward_peek(bits, count, value)) return false;
    bits->bit_position += count;
    return true;
}

bool xx_fse_build_table(const int16_t *normalized, unsigned maximum_symbol,
                        unsigned table_log, xx_fse_table *table) {
    uint16_t symbol_next[XX_FSE_MAX_SYMBOL_VALUE + 1U];
    uint8_t spread[1U << XX_FSE_MAX_TABLE_LOG];
    unsigned table_size;
    unsigned high_threshold;
    unsigned position = 0;
    unsigned step;
    unsigned total = 0;

    if (!normalized || !table || maximum_symbol > XX_FSE_MAX_SYMBOL_VALUE ||
        table_log == 0U || table_log > XX_FSE_MAX_TABLE_LOG) {
        return false;
    }
    table_size = 1U << table_log;
    high_threshold = table_size - 1U;
    for (unsigned symbol = 0; symbol <= maximum_symbol; ++symbol) {
        int count = normalized[symbol];
        if (count < -1) return false;
        total += count < 0 ? 1U : (unsigned)count;
        if (count == -1) {
            spread[high_threshold--] = (uint8_t)symbol;
            symbol_next[symbol] = 1;
        } else {
            symbol_next[symbol] = (uint16_t)count;
        }
    }
    if (total != table_size) return false;

    step = (table_size >> 1) + (table_size >> 3) + 3U;
    for (unsigned symbol = 0; symbol <= maximum_symbol; ++symbol) {
        int count = normalized[symbol];
        for (int index = 0; index < count; ++index) {
            spread[position] = (uint8_t)symbol;
            position = (position + step) & (table_size - 1U);
            while (position > high_threshold) {
                position = (position + step) & (table_size - 1U);
            }
        }
    }
    if (position != 0U) return false;

    table->table_log = table_log;
    table->table_size = table_size;
    for (unsigned index = 0; index < table_size; ++index) {
        unsigned symbol = spread[index];
        unsigned next_state = symbol_next[symbol]++;
        unsigned bit_count = table_log - xx_high_bit32(next_state);
        table->entries[index].symbol = (uint8_t)symbol;
        table->entries[index].bit_count = (uint8_t)bit_count;
        table->entries[index].new_state =
            (uint16_t)((next_state << bit_count) - table_size);
    }
    return true;
}

bool xx_fse_read_table(const uint8_t *source, size_t source_size,
                       unsigned maximum_symbol, unsigned maximum_table_log,
                       xx_fse_table *table, size_t *out_consumed) {
    int16_t normalized[XX_FSE_MAX_SYMBOL_VALUE + 1U] = {0};
    xx_forward_bits bits;
    uint32_t value;
    unsigned table_log;
    int remaining;
    int threshold;
    int bit_count;
    unsigned symbol = 0;
    bool previous_zero = false;

    if (out_consumed) *out_consumed = 0;
    if (!source || source_size == 0U || !table ||
        maximum_symbol > XX_FSE_MAX_SYMBOL_VALUE ||
        maximum_table_log > XX_FSE_MAX_TABLE_LOG) {
        return false;
    }
    bits.data = source;
    bits.size = source_size;
    bits.bit_position = 0;
    if (!xx_forward_read(&bits, 4, &value)) return false;
    table_log = value + 5U;
    if (table_log > maximum_table_log) return false;
    remaining = (1 << table_log) + 1;
    threshold = 1 << table_log;
    bit_count = (int)table_log + 1;

    while (remaining > 1 && symbol <= maximum_symbol) {
        if (previous_zero) {
            unsigned zero_end = symbol;
            for (;;) {
                if (!xx_forward_peek(&bits, 16, &value) || value != UINT32_C(0xFFFF)) {
                    break;
                }
                bits.bit_position += 16U;
                if (zero_end > maximum_symbol - 24U) return false;
                zero_end += 24U;
            }
            for (;;) {
                if (!xx_forward_peek(&bits, 2, &value)) return false;
                if (value != 3U) break;
                bits.bit_position += 2U;
                if (zero_end > maximum_symbol - 3U) return false;
                zero_end += 3U;
            }
            if (!xx_forward_read(&bits, 2, &value) ||
                zero_end > maximum_symbol - value) {
                return false;
            }
            zero_end += value;
            while (symbol < zero_end) normalized[symbol++] = 0;
        }

        {
            int maximum = (2 * threshold - 1) - remaining;
            int count;
            if (!xx_forward_peek(&bits, (unsigned)(bit_count - 1), &value)) {
                return false;
            }
            if ((int)(value & (uint32_t)(threshold - 1)) < maximum) {
                if (!xx_forward_read(&bits, (unsigned)(bit_count - 1), &value)) {
                    return false;
                }
                count = (int)value;
            } else {
                if (!xx_forward_read(&bits, (unsigned)bit_count, &value)) {
                    return false;
                }
                count = (int)value;
                if (count >= threshold) count -= maximum;
            }
            --count;
            remaining -= count < 0 ? -count : count;
            if (remaining < 1) return false;
            normalized[symbol++] = (int16_t)count;
            previous_zero = count == 0;
            while (remaining < threshold) {
                --bit_count;
                threshold >>= 1;
            }
        }
    }
    if (remaining != 1 || symbol == 0U) return false;
    if (!xx_fse_build_table(normalized, symbol - 1U, table_log, table)) {
        return false;
    }
    if (out_consumed) *out_consumed = (bits.bit_position + 7U) >> 3;
    return true;
}

static bool xx_fse_advance_padded(xx_reverse_bits *bits,
                                  const xx_fse_table *table,
                                  unsigned *state, bool *overflow) {
    const xx_fse_entry *entry;
    uint32_t low_bits = 0;
    if (!bits || !table || !state || !overflow ||
        *state >= table->table_size) {
        return false;
    }
    entry = &table->entries[*state];
    if (!xx_reverse_bits_peek_padded(bits, entry->bit_count, &low_bits)) {
        return false;
    }
    *overflow = (size_t)entry->bit_count > bits->bit_position;
    bits->bit_position = *overflow ? 0U :
                         bits->bit_position - entry->bit_count;
    *state = (unsigned)entry->new_state + low_bits;
    return *state < table->table_size;
}

bool xx_fse_decompress(const uint8_t *source, size_t source_size,
                       const xx_fse_table *table, uint8_t *destination,
                       size_t destination_capacity, size_t *out_written) {
    xx_reverse_bits bits;
    uint32_t initial;
    unsigned states[2];
    size_t output = 0;

    if (out_written) *out_written = 0;
    if (!source || !table || !destination || table->table_size == 0U ||
        !xx_reverse_bits_init(&bits, source, source_size) ||
        !xx_reverse_bits_read(&bits, table->table_log, &initial)) {
        return false;
    }
    states[0] = initial;
    if (!xx_reverse_bits_read(&bits, table->table_log, &initial)) return false;
    states[1] = initial;

    for (;;) {
        for (unsigned lane = 0; lane < 2U; ++lane) {
            bool overflow;
            unsigned other = lane ^ 1U;
            if (states[lane] >= table->table_size ||
                output == destination_capacity) {
                return false;
            }
            destination[output++] = table->entries[states[lane]].symbol;
            if (!xx_fse_advance_padded(&bits, table, &states[lane],
                                       &overflow)) {
                return false;
            }
            if (overflow) {
                /* FSE interleaves two states.  The transition which crosses
                   the end marker consumes zero-padded bits; the untouched
                   state still contributes its current symbol. */
                if (states[other] >= table->table_size ||
                    output == destination_capacity) {
                    return false;
                }
                destination[output++] =
                    table->entries[states[other]].symbol;
                if (out_written) *out_written = output;
                return true;
            }
        }
    }
}

bool xx_huf_read_table(const uint8_t *source, size_t source_size,
                       xx_huf_table *table, size_t *out_consumed) {
    uint8_t weights[256] = {0};
    unsigned rank_count[13] = {0};
    unsigned rank_start[13] = {0};
    size_t weight_count;
    size_t consumed;
    uint32_t total = 0;
    unsigned table_log;

    if (!source || source_size == 0U || !table || !out_consumed) {
        return false;
    }
    if (source[0] >= UINT8_C(128)) {
        weight_count = (size_t)source[0] - 127U;
        consumed = (weight_count + 1U) / 2U + 1U;
        if (weight_count >= 256U || consumed > source_size) return false;
        for (size_t index = 0; index < weight_count; index += 2U) {
            uint8_t packed = source[1U + index / 2U];
            weights[index] = packed >> 4;
            if (index + 1U < 256U) weights[index + 1U] = packed & 15U;
        }
    } else {
        xx_fse_table fse_table;
        size_t table_bytes;
        size_t decoded = 0;
        size_t compressed_size = source[0];
        if (compressed_size == 0U || compressed_size + 1U > source_size ||
            !xx_fse_read_table(source + 1U, compressed_size, 255U, 6U,
                               &fse_table, &table_bytes) ||
            table_bytes >= compressed_size ||
            !xx_fse_decompress(source + 1U + table_bytes,
                               compressed_size - table_bytes, &fse_table,
                               weights, 255U, &decoded) || decoded == 0U) {
            return false;
        }
        weight_count = decoded;
        consumed = compressed_size + 1U;
    }

    for (size_t index = 0; index < weight_count; ++index) {
        unsigned weight = weights[index];
        if (weight >= 12U) return false;
        ++rank_count[weight];
        if (weight != 0U) total += 1U << (weight - 1U);
    }
    if (total == 0U) return false;
    table_log = xx_high_bit32(total) + 1U;
    if (table_log > 12U) return false;
    {
        uint32_t remainder = (1U << table_log) - total;
        unsigned final_weight;
        if (remainder == 0U || (remainder & (remainder - 1U)) != 0U) {
            return false;
        }
        final_weight = xx_high_bit32(remainder) + 1U;
        weights[weight_count++] = (uint8_t)final_weight;
        ++rank_count[final_weight];
    }
    if (rank_count[1] < 2U || (rank_count[1] & 1U) != 0U) return false;

    {
        unsigned next = 0;
        for (unsigned weight = 1; weight <= table_log; ++weight) {
            unsigned current = next;
            next += rank_count[weight] << (weight - 1U);
            rank_start[weight] = current;
        }
    }
    for (size_t symbol = 0; symbol < weight_count; ++symbol) {
        unsigned weight = weights[symbol];
        unsigned length;
        unsigned start;
        if (weight == 0U) continue;
        length = 1U << (weight - 1U);
        start = rank_start[weight];
        if (start > (1U << table_log) - length) return false;
        for (unsigned index = 0; index < length; ++index) {
            table->entries[start + index].symbol = (uint8_t)symbol;
            table->entries[start + index].bit_count =
                (uint8_t)(table_log + 1U - weight);
        }
        rank_start[weight] += length;
    }
    table->table_log = table_log;
    *out_consumed = consumed;
    return true;
}

bool xx_huf_decode_1stream(const uint8_t *source, size_t source_size,
                           const xx_huf_table *table,
                           uint8_t *destination, size_t destination_size) {
    xx_reverse_bits bits;
    if (!source || !table || !destination || table->table_log == 0U ||
        table->table_log > 12U ||
        !xx_reverse_bits_init(&bits, source, source_size)) return false;
    for (size_t index = 0; index < destination_size; ++index) {
        uint32_t slot;
        const xx_huf_entry *entry;
        if (!xx_reverse_bits_peek_padded(&bits, table->table_log, &slot) ||
            slot >= (1U << table->table_log)) {
            return false;
        }
        entry = &table->entries[slot];
        if (entry->bit_count == 0U ||
            !xx_reverse_bits_skip(&bits, entry->bit_count)) {
            return false;
        }
        destination[index] = entry->symbol;
    }
    return bits.bit_position == 0U;
}

bool xx_huf_decode_4streams(const uint8_t *source, size_t source_size,
                            const xx_huf_table *table,
                            uint8_t *destination, size_t destination_size) {
    size_t lengths[4];
    size_t segment_size;
    size_t output_offsets[5];
    size_t input_offset = 6U;

    if (!source || !table || !destination || source_size < 10U ||
        destination_size < 6U) return false;
    lengths[0] = (size_t)source[0] | ((size_t)source[1] << 8);
    lengths[1] = (size_t)source[2] | ((size_t)source[3] << 8);
    lengths[2] = (size_t)source[4] | ((size_t)source[5] << 8);
    if (lengths[0] > source_size - 6U ||
        lengths[1] > source_size - 6U - lengths[0] ||
        lengths[2] > source_size - 6U - lengths[0] - lengths[1]) {
        return false;
    }
    lengths[3] = source_size - 6U - lengths[0] - lengths[1] - lengths[2];
    segment_size = (destination_size + 3U) / 4U;
    output_offsets[0] = 0U;
    output_offsets[1] = segment_size;
    output_offsets[2] = segment_size * 2U;
    output_offsets[3] = segment_size * 3U;
    output_offsets[4] = destination_size;
    if (output_offsets[3] > destination_size) return false;
    for (unsigned lane = 0; lane < 4U; ++lane) {
        size_t lane_output = output_offsets[lane + 1U] - output_offsets[lane];
        if (lengths[lane] == 0U ||
            !xx_huf_decode_1stream(source + input_offset, lengths[lane], table,
                                   destination + output_offsets[lane],
                                   lane_output)) {
            return false;
        }
        input_offset += lengths[lane];
    }
    return input_offset == source_size;
}

bool xx_huf_decompress(const uint8_t *source, size_t source_size,
                       uint8_t *destination, size_t destination_size) {
    xx_huf_table table = {0};
    size_t tree_size;
    const uint8_t *payload;
    size_t payload_size;

    if (!source || !destination || destination_size == 0U || source_size == 0U) {
        return false;
    }
    if (source_size == destination_size) {
        for (size_t index = 0; index < destination_size; ++index) {
            destination[index] = source[index];
        }
        return true;
    }
    if (source_size == 1U) {
        for (size_t index = 0; index < destination_size; ++index) {
            destination[index] = source[0];
        }
        return true;
    }
    if (!xx_huf_read_table(source, source_size, &table, &tree_size) ||
        tree_size >= source_size) {
        return false;
    }
    payload = source + tree_size;
    payload_size = source_size - tree_size;
    if (payload_size < 10U) return false;
    {
        return xx_huf_decode_4streams(payload, payload_size, &table,
                                      destination, destination_size);
    }
}

/* -------------------------------------------------------- Shannon Entropy */

double xx_entropy_calculate(const void *data, size_t size) {
    if (!data || size == 0) {
        return 0.0;
    }

    if (xx_is_avx2_enabled() && size >= 64) {
        return xx_entropy_calculate_avx2(data, size);
    }
    if (xx_is_sse2_enabled() && size >= 32) {
        return xx_entropy_calculate_sse2(data, size);
    }

    const uint8_t *p = (const uint8_t *)data;
    uint32_t c0[256] = {0};
    uint32_t c1[256] = {0};
    uint32_t c2[256] = {0};
    uint32_t c3[256] = {0};

    size_t i = 0;
    size_t limit = size & ~((size_t)3);
    for (; i < limit; i += 4) {
        c0[p[i]]++;
        c1[p[i + 1]]++;
        c2[p[i + 2]]++;
        c3[p[i + 3]]++;
    }
    for (; i < size; i++) {
        c0[p[i]]++;
    }

    double sum = 0.0;
    for (int k = 0; k < 256; k++) {
        uint64_t total = (uint64_t)c0[k] + c1[k] + c2[k] + c3[k];
        if (total > 0) {
            double c = (double)total;
            sum += c * xx_rt_log(c);
        }
    }

    double dsize = (double)size;
    double result = xx_rt_log(dsize) - (sum / dsize);
    const double inv_log2 = 1.44269504088896340736; /* 1.0 / ln(2.0) */

    return result * inv_log2;
}

double xx_entropy(const void *data, size_t size) {
    return xx_entropy_calculate(data, size);
}
