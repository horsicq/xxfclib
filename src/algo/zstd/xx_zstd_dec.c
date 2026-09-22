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

/* Native, bounded Zstandard frame decoder. The implementation follows the
   public Zstandard framing and entropy-format specification and deliberately
   owns all state, parsing, and match copying inside xxfclib. */

#include "xx_zstd_dec.h"

#include "../entropy/xx_entropy.h"
#include "xxfclib/memory/xx_memory.h"

#include <stdint.h>

#define XX_ZSTD_MAGIC UINT32_C(0xFD2FB528)
#define XX_ZSTD_SKIP_MAGIC UINT32_C(0x184D2A50)
#define XX_ZSTD_BLOCK_MAX (128U * 1024U)

typedef struct xx_zstd_context {
    xx_fse_table literal_lengths;
    xx_fse_table offsets;
    xx_fse_table match_lengths;
    xx_huf_table huffman;
    bool literal_lengths_valid;
    bool offsets_valid;
    bool match_lengths_valid;
    bool huffman_valid;
    size_t repeated[3];
    uint8_t *literals;
} xx_zstd_context;

static const uint32_t xx_zstd_ll_base[36] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512,
    1024, 2048, 4096, 8192, 16384, 32768, 65536
};

static const uint8_t xx_zstd_ll_bits[36] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 15, 16
};

static const uint32_t xx_zstd_ml_base[53] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 37, 39, 41, 43, 47, 51, 59,
    67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195,
    16387, 32771, 65539
};

static const uint8_t xx_zstd_ml_bits[53] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16
};

static const int16_t xx_zstd_ll_default[36] = {
    4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
    -1, -1, -1, -1
};

static const int16_t xx_zstd_of_default[29] = {
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1
};

static const int16_t xx_zstd_ml_default[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1,
    -1, -1, -1, -1, -1, -1
};

static uint32_t xx_zstd_read24(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16);
}

static uint32_t xx_zstd_read32(const uint8_t *data) {
    return xx_zstd_read24(data) | ((uint32_t)data[3] << 24);
}

static uint64_t xx_zstd_read64(const uint8_t *data) {
    return (uint64_t)xx_zstd_read32(data) |
           ((uint64_t)xx_zstd_read32(data + 4) << 32);
}

static uint64_t xx_zstd_rotl64(uint64_t value, unsigned count) {
    return (value << count) | (value >> (64U - count));
}

static uint64_t xx_zstd_xxh64_round(uint64_t state, uint64_t value) {
    state += value * UINT64_C(14029467366897019727);
    state = xx_zstd_rotl64(state, 31);
    return state * UINT64_C(11400714785074694791);
}

static uint64_t xx_zstd_xxh64(const uint8_t *data, size_t size) {
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint64_t hash;
    if (size >= 32U) {
        uint64_t a = UINT64_C(11400714785074694791) +
                     UINT64_C(14029467366897019727);
        uint64_t b = UINT64_C(14029467366897019727);
        uint64_t c = 0;
        uint64_t d = 0U - UINT64_C(11400714785074694791);
        const uint8_t *limit = end - 32U;
        do {
            a = xx_zstd_xxh64_round(a, xx_zstd_read64(cursor)); cursor += 8;
            b = xx_zstd_xxh64_round(b, xx_zstd_read64(cursor)); cursor += 8;
            c = xx_zstd_xxh64_round(c, xx_zstd_read64(cursor)); cursor += 8;
            d = xx_zstd_xxh64_round(d, xx_zstd_read64(cursor)); cursor += 8;
        } while (cursor <= limit);
        hash = xx_zstd_rotl64(a, 1) + xx_zstd_rotl64(b, 7) +
               xx_zstd_rotl64(c, 12) + xx_zstd_rotl64(d, 18);
        a = xx_zstd_xxh64_round(0, a); hash ^= a;
        hash = hash * UINT64_C(11400714785074694791) +
               UINT64_C(9650029242287828579);
        b = xx_zstd_xxh64_round(0, b); hash ^= b;
        hash = hash * UINT64_C(11400714785074694791) +
               UINT64_C(9650029242287828579);
        c = xx_zstd_xxh64_round(0, c); hash ^= c;
        hash = hash * UINT64_C(11400714785074694791) +
               UINT64_C(9650029242287828579);
        d = xx_zstd_xxh64_round(0, d); hash ^= d;
        hash = hash * UINT64_C(11400714785074694791) +
               UINT64_C(9650029242287828579);
    } else {
        hash = UINT64_C(2870177450012600261);
    }
    hash += size;
    while ((size_t)(end - cursor) >= 8U) {
        uint64_t value = xx_zstd_xxh64_round(0, xx_zstd_read64(cursor));
        hash ^= value;
        hash = xx_zstd_rotl64(hash, 27) * UINT64_C(11400714785074694791) +
               UINT64_C(9650029242287828579);
        cursor += 8;
    }
    if ((size_t)(end - cursor) >= 4U) {
        hash ^= (uint64_t)xx_zstd_read32(cursor) *
                UINT64_C(11400714785074694791);
        hash = xx_zstd_rotl64(hash, 23) * UINT64_C(14029467366897019727) +
               UINT64_C(1609587929392839161);
        cursor += 4;
    }
    while (cursor < end) {
        hash ^= (uint64_t)*cursor++ * UINT64_C(2870177450012600261);
        hash = xx_zstd_rotl64(hash, 11) * UINT64_C(11400714785074694791);
    }
    hash ^= hash >> 33;
    hash *= UINT64_C(14029467366897019727);
    hash ^= hash >> 29;
    hash *= UINT64_C(1609587929392839161);
    return hash ^ (hash >> 32);
}

static bool xx_zstd_make_rle_table(xx_fse_table *table, unsigned symbol,
                                   unsigned maximum_symbol) {
    if (!table || symbol > maximum_symbol) return false;
    table->table_log = 0U;
    table->table_size = 1U;
    table->entries[0].symbol = (uint8_t)symbol;
    table->entries[0].new_state = 0U;
    table->entries[0].bit_count = 0U;
    return true;
}

static bool xx_zstd_read_sequence_table(const uint8_t **input,
                                        const uint8_t *end, unsigned mode,
                                        const int16_t *defaults,
                                        unsigned default_maximum,
                                        unsigned default_log,
                                        unsigned maximum_symbol,
                                        unsigned maximum_log,
                                        xx_fse_table *table, bool *valid) {
    size_t consumed = 0;
    if (mode == 3U) return *valid;
    if (mode == 0U) {
        if (!xx_fse_build_table(defaults, default_maximum, default_log, table)) {
            return false;
        }
    } else if (mode == 1U) {
        if (*input == end ||
            !xx_zstd_make_rle_table(table, *(*input)++, maximum_symbol)) {
            return false;
        }
    } else if (mode == 2U) {
        if (!xx_fse_read_table(*input, (size_t)(end - *input), maximum_symbol,
                               maximum_log, table, &consumed) ||
            consumed == 0U || consumed > (size_t)(end - *input)) {
            return false;
        }
        *input += consumed;
    } else {
        return false;
    }
    *valid = true;
    return true;
}

static bool xx_zstd_fse_state_init(xx_reverse_bits *bits,
                                   const xx_fse_table *table,
                                   unsigned *state) {
    uint32_t value = 0;
    if (table->table_size == 0U) return false;
    if (table->table_log != 0U &&
        !xx_reverse_bits_read(bits, table->table_log, &value)) return false;
    if (value >= table->table_size) return false;
    *state = value;
    return true;
}

static bool xx_zstd_fse_state_advance(xx_reverse_bits *bits,
                                      const xx_fse_table *table,
                                      unsigned *state) {
    const xx_fse_entry *entry;
    uint32_t value = 0;
    if (*state >= table->table_size) return false;
    entry = &table->entries[*state];
    if (entry->bit_count != 0U &&
        !xx_reverse_bits_read(bits, entry->bit_count, &value)) return false;
    *state = (unsigned)entry->new_state + value;
    return *state < table->table_size;
}

static bool xx_zstd_copy_match(uint8_t *output, size_t destination_size,
                               size_t frame_start, size_t *output_position,
                               size_t distance, size_t length,
                               size_t window_size, size_t block_limit) {
    size_t position = *output_position;
    size_t frame_output = position - frame_start;
    if (distance == 0U || distance > frame_output || distance > window_size ||
        length > destination_size - position || length > block_limit) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        output[position] = output[position - distance];
        ++position;
    }
    *output_position = position;
    return true;
}

static bool xx_zstd_decode_sequences(xx_zstd_context *context,
                                     const uint8_t *source,
                                     size_t source_size, size_t sequence_count,
                                     const uint8_t *literals,
                                     size_t literal_count, uint8_t *output,
                                     size_t destination_size,
                                     size_t frame_start,
                                     size_t *output_position,
                                     size_t window_size,
                                     size_t block_output_limit) {
    xx_reverse_bits bits;
    unsigned ll_state;
    unsigned of_state;
    unsigned ml_state;
    size_t literal_position = 0U;
    size_t block_start = *output_position;

    if (sequence_count == 0U) {
        if (literal_count > destination_size - *output_position ||
            literal_count > block_output_limit) return false;
        for (size_t index = 0; index < literal_count; ++index) {
            output[(*output_position)++] = literals[index];
        }
        return true;
    }
    if (!source || source_size == 0U ||
        !xx_reverse_bits_init(&bits, source, source_size) ||
        !xx_zstd_fse_state_init(&bits, &context->literal_lengths, &ll_state) ||
        !xx_zstd_fse_state_init(&bits, &context->offsets, &of_state) ||
        !xx_zstd_fse_state_init(&bits, &context->match_lengths, &ml_state)) {
        return false;
    }

    for (size_t sequence = 0; sequence < sequence_count; ++sequence) {
        unsigned ll_symbol;
        unsigned of_symbol;
        unsigned ml_symbol;
        uint32_t extra = 0;
        size_t literal_length;
        size_t match_length;
        size_t distance;
        if (ll_state >= context->literal_lengths.table_size ||
            of_state >= context->offsets.table_size ||
            ml_state >= context->match_lengths.table_size) return false;
        ll_symbol = context->literal_lengths.entries[ll_state].symbol;
        of_symbol = context->offsets.entries[of_state].symbol;
        ml_symbol = context->match_lengths.entries[ml_state].symbol;
        if (ll_symbol >= 36U || ml_symbol >= 53U || of_symbol >= 32U) {
            return false;
        }

        if (of_symbol != 0U &&
            !xx_reverse_bits_read(&bits, of_symbol, &extra)) return false;
        if (of_symbol == 0U) {
            if (ll_symbol == 0U) {
                distance = context->repeated[1];
                context->repeated[1] = context->repeated[0];
                context->repeated[0] = distance;
            } else {
                distance = context->repeated[0];
            }
        } else if (of_symbol == 1U) {
            if (ll_symbol == 0U) {
                if (extra == 0U) {
                    distance = context->repeated[2];
                    context->repeated[2] = context->repeated[1];
                    context->repeated[1] = context->repeated[0];
                    context->repeated[0] = distance;
                } else {
                    if (context->repeated[0] <= 1U) return false;
                    distance = context->repeated[0] - 1U;
                    context->repeated[2] = context->repeated[1];
                    context->repeated[1] = context->repeated[0];
                    context->repeated[0] = distance;
                }
            } else if (extra == 0U) {
                distance = context->repeated[1];
                context->repeated[1] = context->repeated[0];
                context->repeated[0] = distance;
            } else {
                distance = context->repeated[2];
                context->repeated[2] = context->repeated[1];
                context->repeated[1] = context->repeated[0];
                context->repeated[0] = distance;
            }
        } else {
            distance = ((size_t)1U << of_symbol) - 3U + (size_t)extra;
            context->repeated[2] = context->repeated[1];
            context->repeated[1] = context->repeated[0];
            context->repeated[0] = distance;
        }

        extra = 0U;
        if (xx_zstd_ml_bits[ml_symbol] != 0U &&
            !xx_reverse_bits_read(&bits, xx_zstd_ml_bits[ml_symbol], &extra)) {
            return false;
        }
        match_length = (size_t)xx_zstd_ml_base[ml_symbol] + extra;
        extra = 0U;
        if (xx_zstd_ll_bits[ll_symbol] != 0U &&
            !xx_reverse_bits_read(&bits, xx_zstd_ll_bits[ll_symbol], &extra)) {
            return false;
        }
        literal_length = (size_t)xx_zstd_ll_base[ll_symbol] + extra;
        {
            size_t block_output = *output_position - block_start;
        if (block_output > block_output_limit ||
            literal_length > literal_count - literal_position ||
            literal_length > destination_size - *output_position ||
            literal_length > block_output_limit - block_output) {
            return false;
        }
        }
        for (size_t index = 0; index < literal_length; ++index) {
            output[(*output_position)++] = literals[literal_position++];
        }
        if (*output_position - block_start > block_output_limit ||
            match_length > block_output_limit - (*output_position - block_start) ||
            !xx_zstd_copy_match(output, destination_size, frame_start,
                                output_position, distance, match_length,
                                window_size, block_output_limit)) {
            return false;
        }
        if (sequence + 1U < sequence_count &&
            (!xx_zstd_fse_state_advance(&bits, &context->literal_lengths,
                                        &ll_state) ||
             !xx_zstd_fse_state_advance(&bits, &context->match_lengths,
                                        &ml_state) ||
             !xx_zstd_fse_state_advance(&bits, &context->offsets,
                                        &of_state))) {
            return false;
        }
    }

    if (*output_position - block_start > block_output_limit ||
        bits.bit_position != 0U ||
        literal_count - literal_position > destination_size - *output_position ||
        literal_count - literal_position >
            block_output_limit - (*output_position - block_start)) {
        return false;
    }
    while (literal_position < literal_count) {
        output[(*output_position)++] = literals[literal_position++];
    }
    return true;
}

static bool xx_zstd_decode_compressed_block(xx_zstd_context *context,
                                            const uint8_t *source,
                                            size_t source_size,
                                            uint8_t *output,
                                            size_t destination_size,
                                            size_t frame_start,
                                            size_t *output_position,
                                            size_t window_size,
                                            size_t block_output_limit) {
    const uint8_t *input = source;
    const uint8_t *end = source + source_size;
    const uint8_t *literal_stream;
    const uint8_t *literals;
    size_t literal_size;
    size_t literal_compressed_size;
    size_t literal_header_size;
    unsigned literal_type;
    unsigned stream_mode = 0U;
    size_t sequence_count;

    if (!context || !source || source_size < 2U || !context->literals) {
        return false;
    }
    literal_type = input[0] & 3U;
    if (literal_type < 2U) {
        unsigned size_format = (input[0] >> 2) & 3U;
        if (size_format == 0U || size_format == 2U) {
            literal_header_size = 1U;
            literal_size = input[0] >> 3;
        } else if (size_format == 1U) {
            if (source_size < 2U) return false;
            literal_header_size = 2U;
            literal_size = ((size_t)input[0] | ((size_t)input[1] << 8)) >> 4;
        } else {
            if (source_size < 3U) return false;
            literal_header_size = 3U;
            literal_size = (size_t)xx_zstd_read24(input) >> 4;
        }
        literal_compressed_size = literal_type == 0U ? literal_size : 1U;
    } else {
        uint32_t packed;
        unsigned size_format = (input[0] >> 2) & 3U;
        unsigned following = size_format < 2U ? 2U : size_format + 1U;
        unsigned size_bits = following * 4U - 2U;
        uint32_t mask = ((uint32_t)16U << size_bits) - 1U;
        if (source_size < (size_t)following + 1U) return false;
        packed = xx_zstd_read32(input + 1U);
        literal_header_size = (size_t)following + 1U;
        literal_size = (size_t)((xx_zstd_read32(input) >> 4) & mask);
        literal_compressed_size = (size_t)((packed >> size_bits) & mask);
        stream_mode = (input[0] & 12U) == 0U ? 1U : 4U;
        if (literal_compressed_size == 0U) return false;
    }
    if (literal_size > XX_ZSTD_BLOCK_MAX ||
        literal_header_size > source_size ||
        literal_compressed_size > source_size - literal_header_size) {
        return false;
    }
    literal_stream = input + literal_header_size;
    input = literal_stream + literal_compressed_size;
    if (input >= end) return false;

    if (literal_type == 0U) {
        literals = literal_stream;
    } else if (literal_type == 1U) {
        for (size_t index = 0; index < literal_size; ++index) {
            context->literals[index] = literal_stream[0];
        }
        literals = context->literals;
    } else {
        const uint8_t *payload = literal_stream;
        size_t payload_size = literal_compressed_size;
        if (literal_type == 2U) {
            size_t tree_size = 0U;
            if (!xx_huf_read_table(payload, payload_size, &context->huffman,
                                   &tree_size) || tree_size >= payload_size) {
                return false;
            }
            context->huffman_valid = true;
            payload += tree_size;
            payload_size -= tree_size;
        } else if (!context->huffman_valid) {
            return false;
        }
        if ((stream_mode == 1U &&
             !xx_huf_decode_1stream(payload, payload_size, &context->huffman,
                                    context->literals, literal_size)) ||
            (stream_mode == 4U &&
             !xx_huf_decode_4streams(payload, payload_size, &context->huffman,
                                     context->literals, literal_size))) {
            return false;
        }
        literals = context->literals;
    }

    sequence_count = *input++;
    if (sequence_count > 127U) {
        size_t high = sequence_count - 128U;
        uint8_t low;
        if (input == end) return false;
        low = *input++;
        if (high == 127U) {
            if (input == end) return false;
            high = (size_t)*input++ + 127U;
        }
        sequence_count = (high << 8) + low;
    }
    if (sequence_count == 0U) {
        if (input != end) return false;
        return xx_zstd_decode_sequences(context, NULL, 0U, 0U, literals,
                                        literal_size, output, destination_size,
                                        frame_start, output_position,
                                        window_size, block_output_limit);
    }
    if (input == end) return false;
    {
        unsigned modes = *input++;
        if ((modes & 3U) != 0U ||
            !xx_zstd_read_sequence_table(
                &input, end, modes >> 6, xx_zstd_ll_default, 35U, 6U,
                35U, 9U, &context->literal_lengths,
                &context->literal_lengths_valid) ||
            !xx_zstd_read_sequence_table(
                &input, end, (modes >> 4) & 3U, xx_zstd_of_default, 28U, 5U,
                31U, 8U, &context->offsets, &context->offsets_valid) ||
            !xx_zstd_read_sequence_table(
                &input, end, (modes >> 2) & 3U, xx_zstd_ml_default, 52U, 6U,
                52U, 9U, &context->match_lengths,
                &context->match_lengths_valid) ||
            input == end) {
            return false;
        }
    }
    return xx_zstd_decode_sequences(context, input, (size_t)(end - input),
                                    sequence_count, literals, literal_size,
                                    output, destination_size, frame_start,
                                    output_position, window_size,
                                    block_output_limit);
}

static bool xx_zstd_read_variable(const uint8_t **input, const uint8_t *end,
                                  unsigned count, uint64_t *value) {
    uint64_t result = 0U;
    if (count > 8U || (size_t)(end - *input) < count) return false;
    for (unsigned index = 0; index < count; ++index) {
        result |= (uint64_t)(*input)[index] << (index * 8U);
    }
    *input += count;
    *value = result;
    return true;
}

bool xx_zstd_decode_frames(const void *source, size_t source_size,
                           void *destination, size_t destination_size,
                           size_t *out_written) {
    const uint8_t *input = (const uint8_t *)source;
    const uint8_t *end;
    uint8_t *output = (uint8_t *)destination;
    uint8_t empty_output = 0U;
    size_t output_position = 0U;
    bool saw_frame = false;
    xx_zstd_context context;

    if (out_written) *out_written = 0U;
    if ((!input && source_size != 0U) || (!output && destination_size != 0U)) {
        return false;
    }
    if (!output) output = &empty_output;
    end = input + source_size;
    context.literals = (uint8_t *)xx_mem_alloc(XX_ZSTD_BLOCK_MAX);
    if (!context.literals) return false;

    while (input < end) {
        uint32_t magic;
        uint8_t descriptor;
        unsigned content_size_flag;
        unsigned dictionary_flag;
        unsigned dictionary_size;
        unsigned content_size_bytes;
        bool single_segment;
        bool checksum;
        uint64_t frame_content_size = 0U;
        uint64_t dictionary_id = 0U;
        uint64_t window_size64;
        size_t window_size;
        size_t frame_start;
        bool last_block = false;

        if ((size_t)(end - input) < 4U) goto error;
        magic = xx_zstd_read32(input);
        input += 4;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_ZSTD_SKIP_MAGIC) {
            uint32_t skip_size;
            if ((size_t)(end - input) < 4U) goto error;
            skip_size = xx_zstd_read32(input);
            input += 4;
            if ((size_t)(end - input) < skip_size) goto error;
            input += skip_size;
            continue;
        }
        if (magic != XX_ZSTD_MAGIC || input == end) goto error;
        descriptor = *input++;
        if ((descriptor & UINT8_C(0x18)) != 0U) goto error;
        content_size_flag = descriptor >> 6;
        single_segment = (descriptor & UINT8_C(0x20)) != 0U;
        checksum = (descriptor & UINT8_C(0x04)) != 0U;
        dictionary_flag = descriptor & 3U;
        if (!single_segment) {
            uint8_t window_descriptor;
            unsigned exponent;
            unsigned mantissa;
            if (input == end) goto error;
            window_descriptor = *input++;
            exponent = window_descriptor >> 3;
            mantissa = window_descriptor & 7U;
            if (exponent >= 54U) goto error;
            window_size64 = UINT64_C(1) << (10U + exponent);
            window_size64 += (window_size64 >> 3) * mantissa;
        } else {
            window_size64 = 0U;
        }
        dictionary_size = dictionary_flag == 0U ? 0U :
                          dictionary_flag == 1U ? 1U :
                          dictionary_flag == 2U ? 2U : 4U;
        if (!xx_zstd_read_variable(&input, end, dictionary_size,
                                   &dictionary_id) || dictionary_id != 0U) {
            goto error;
        }
        content_size_bytes = content_size_flag == 0U ?
                                 (single_segment ? 1U : 0U) :
                             content_size_flag == 1U ? 2U :
                             content_size_flag == 2U ? 4U : 8U;
        if (!xx_zstd_read_variable(&input, end, content_size_bytes,
                                   &frame_content_size)) goto error;
        if (content_size_bytes == 2U) frame_content_size += 256U;
        if (single_segment) window_size64 = frame_content_size;
        if (window_size64 > SIZE_MAX) goto error;
        window_size = (size_t)window_size64;
        frame_start = output_position;
        context.literal_lengths_valid = false;
        context.offsets_valid = false;
        context.match_lengths_valid = false;
        context.huffman_valid = false;
        context.repeated[0] = 1U;
        context.repeated[1] = 4U;
        context.repeated[2] = 8U;

        while (!last_block) {
            uint32_t header;
            unsigned block_type;
            size_t block_size;
            size_t block_limit = window_size < XX_ZSTD_BLOCK_MAX ?
                                     window_size : XX_ZSTD_BLOCK_MAX;
            size_t block_start = output_position;
            if ((size_t)(end - input) < 3U) goto error;
            header = xx_zstd_read24(input);
            input += 3;
            last_block = (header & 1U) != 0U;
            block_type = (header >> 1) & 3U;
            block_size = header >> 3;
            if (block_type == 3U || block_size > XX_ZSTD_BLOCK_MAX) goto error;
            if (block_type == 0U) {
                if (block_size > (size_t)(end - input) ||
                    block_size > destination_size - output_position ||
                    block_size > block_limit) goto error;
                for (size_t index = 0; index < block_size; ++index) {
                    output[output_position++] = input[index];
                }
                input += block_size;
            } else if (block_type == 1U) {
                if (input == end || block_size > destination_size - output_position ||
                    block_size > block_limit) goto error;
                for (size_t index = 0; index < block_size; ++index) {
                    output[output_position++] = *input;
                }
                ++input;
            } else {
                if (block_size == 0U || block_size > (size_t)(end - input) ||
                    !xx_zstd_decode_compressed_block(
                        &context, input, block_size, output, destination_size,
                        frame_start, &output_position, window_size,
                        block_limit)) goto error;
                input += block_size;
            }
            if (output_position - block_start > block_limit) goto error;
        }
        if (content_size_bytes != 0U &&
            frame_content_size != output_position - frame_start) goto error;
        if (checksum) {
            if ((size_t)(end - input) < 4U ||
                xx_zstd_read32(input) != (uint32_t)xx_zstd_xxh64(
                    output + frame_start, output_position - frame_start)) {
                goto error;
            }
            input += 4;
        }
        saw_frame = true;
    }
    xx_mem_free(context.literals);
    if (!saw_frame || output_position != destination_size) return false;
    if (out_written) *out_written = output_position;
    return true;

error:
    xx_mem_free(context.literals);
    return false;
}
