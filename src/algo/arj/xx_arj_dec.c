/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native ARJ methods 1-4 decoder. The implementation follows the ARJ bitstream
 * definitions and was independently written for xxfclib using XArchive's
 * MIT-licensed decoder as a compatibility reference.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/arj/xx_arj_dec.h"

#include <string.h>

#define XX_ARJ_DIC_SIZE 26624U
#define XX_ARJ_THRESHOLD 3U
#define XX_ARJ_MAX_MATCH 256U
#define XX_ARJ_CODE_BITS 16U
#define XX_ARJ_NT 19U
#define XX_ARJ_NP 17U
#define XX_ARJ_NC 510U
#define XX_ARJ_NPT 19U
#define XX_ARJ_CTABLE_SIZE 4096U
#define XX_ARJ_PTABLE_SIZE 256U
#define XX_ARJ_TREE_SIZE (2U * XX_ARJ_NC - 1U)

typedef struct xx_arj_decoder_s {
    const uint8_t *input;
    size_t input_size;
    size_t input_pos;
    uint16_t bit_buf;
    uint8_t sub_bit_buf;
    int bit_count;
    uint16_t block_size;
    int get_len;
    uint16_t get_buf;
    uint16_t left[XX_ARJ_TREE_SIZE];
    uint16_t right[XX_ARJ_TREE_SIZE];
    uint8_t c_len[XX_ARJ_NC];
    uint16_t c_table[XX_ARJ_CTABLE_SIZE];
    uint8_t pt_len[XX_ARJ_NPT];
    uint16_t pt_table[XX_ARJ_PTABLE_SIZE];
    bool error;
} xx_arj_decoder;

static bool xx_arj_fill_bits(xx_arj_decoder *state, unsigned count) {
    if (!state || state->error || count > XX_ARJ_CODE_BITS) {
        if (state) state->error = true;
        return false;
    }

    state->bit_buf = (uint16_t)((uint32_t)state->bit_buf << count);
    while ((int)count > state->bit_count) {
        state->bit_buf |= (uint16_t)((uint16_t)state->sub_bit_buf <<
                                    (count - (unsigned)state->bit_count));
        count -= (unsigned)state->bit_count;
        state->sub_bit_buf = state->input_pos < state->input_size
                                 ? state->input[state->input_pos++]
                                 : 0U;
        state->bit_count = 8;
    }
    state->bit_buf |= (uint16_t)(state->sub_bit_buf >>
                                (state->bit_count - (int)count));
    state->bit_count -= (int)count;
    return true;
}

static uint16_t xx_arj_get_bits(xx_arj_decoder *state, unsigned count) {
    uint16_t result;
    if (!state || state->error || count > XX_ARJ_CODE_BITS) {
        if (state) state->error = true;
        return 0U;
    }
    result = count == 0U ? 0U : (uint16_t)(state->bit_buf >>
                                           (XX_ARJ_CODE_BITS - count));
    if (!xx_arj_fill_bits(state, count)) return 0U;
    return result;
}

static bool xx_arj_make_table(xx_arj_decoder *state,
                              unsigned symbols,
                              const uint8_t *lengths,
                              unsigned table_bits,
                              uint16_t *table,
                              unsigned table_size) {
    uint16_t count[17] = {0};
    uint16_t weight[17] = {0};
    uint16_t start[18] = {0};
    unsigned i;
    unsigned available;
    unsigned position;
    unsigned jut_bits;
    uint16_t mask;

    if (!state || !lengths || !table || table_bits == 0U ||
        table_bits >= 17U || table_size < (1U << table_bits)) {
        if (state) state->error = true;
        return false;
    }
    for (i = 0U; i < symbols; ++i) {
        if (lengths[i] >= 17U) {
            state->error = true;
            return false;
        }
        ++count[lengths[i]];
    }
    for (i = 1U; i <= 16U; ++i) {
        start[i + 1U] = (uint16_t)(start[i] +
                                  (uint16_t)(count[i] << (16U - i)));
    }
    if (start[17] != 0U) {
        state->error = true;
        return false;
    }

    jut_bits = 16U - table_bits;
    for (i = 1U; i <= table_bits; ++i) {
        start[i] = (uint16_t)(start[i] >> jut_bits);
        weight[i] = (uint16_t)(1U << (table_bits - i));
    }
    for (i = table_bits + 1U; i <= 16U; ++i) {
        weight[i] = (uint16_t)(1U << (16U - i));
    }

    position = (unsigned)(start[table_bits + 1U] >> jut_bits);
    if (position != 0U) {
        while (position < (1U << table_bits)) table[position++] = 0U;
    }

    available = symbols;
    mask = (uint16_t)(1U << (15U - table_bits));
    for (i = 0U; i < symbols; ++i) {
        unsigned length = lengths[i];
        unsigned code;
        unsigned next;
        if (length == 0U) continue;
        code = start[length];
        next = code + weight[length];
        if (length <= table_bits) {
            if (next > table_size) {
                state->error = true;
                return false;
            }
            while (code < next) table[code++] = (uint16_t)i;
        } else {
            uint16_t *slot;
            unsigned remaining = length - table_bits;
            unsigned table_index = code >> jut_bits;
            if (table_index >= table_size) {
                state->error = true;
                return false;
            }
            slot = &table[table_index];
            while (remaining-- != 0U) {
                if (*slot == 0U) {
                    if (available >= XX_ARJ_TREE_SIZE) {
                        state->error = true;
                        return false;
                    }
                    state->left[available] = 0U;
                    state->right[available] = 0U;
                    *slot = (uint16_t)available++;
                }
                if (*slot >= XX_ARJ_TREE_SIZE) {
                    state->error = true;
                    return false;
                }
                slot = (code & mask) ? &state->right[*slot]
                                     : &state->left[*slot];
                code <<= 1U;
            }
            *slot = (uint16_t)i;
        }
        start[length] = (uint16_t)next;
    }
    return true;
}

static bool xx_arj_read_pt_lengths(xx_arj_decoder *state,
                                   unsigned count,
                                   unsigned width,
                                   int special) {
    unsigned n = xx_arj_get_bits(state, width);
    unsigned i;
    if (state->error || count > XX_ARJ_NPT) return false;
    if (n == 0U) {
        unsigned symbol = xx_arj_get_bits(state, width);
        if (state->error || symbol >= count || symbol >= XX_ARJ_NPT) {
            state->error = true;
            return false;
        }
        xx_rt_memset(state->pt_len, 0, count);
        for (i = 0U; i < XX_ARJ_PTABLE_SIZE; ++i)
            state->pt_table[i] = (uint16_t)symbol;
        return true;
    }
    if (n > count || n > XX_ARJ_NPT) {
        state->error = true;
        return false;
    }
    i = 0U;
    while (i < n) {
        unsigned length = state->bit_buf >> 13U;
        if (length == 7U) {
            uint16_t test = (uint16_t)(1U << 12U);
            while ((test & state->bit_buf) != 0U) {
                test >>= 1U;
                ++length;
            }
        }
        if (!xx_arj_fill_bits(state, length < 7U ? 3U : length - 3U))
            return false;
        state->pt_len[i++] = (uint8_t)length;
        if ((int)i == special) {
            unsigned skip = xx_arj_get_bits(state, 2U);
            if (state->error || skip > count - i) {
                state->error = true;
                return false;
            }
            while (skip-- != 0U) state->pt_len[i++] = 0U;
        }
    }
    while (i < count) state->pt_len[i++] = 0U;
    return xx_arj_make_table(state, count, state->pt_len, 8U,
                             state->pt_table, XX_ARJ_PTABLE_SIZE);
}

static bool xx_arj_read_c_lengths(xx_arj_decoder *state) {
    unsigned n = xx_arj_get_bits(state, 9U);
    unsigned i;
    if (state->error) return false;
    if (n == 0U) {
        unsigned symbol = xx_arj_get_bits(state, 9U);
        if (state->error || symbol >= XX_ARJ_NC) {
            state->error = true;
            return false;
        }
        xx_rt_memset(state->c_len, 0, sizeof(state->c_len));
        for (i = 0U; i < XX_ARJ_CTABLE_SIZE; ++i)
            state->c_table[i] = (uint16_t)symbol;
        return true;
    }
    if (n > XX_ARJ_NC) {
        state->error = true;
        return false;
    }
    i = 0U;
    while (i < n) {
        unsigned symbol = state->pt_table[state->bit_buf >> 8U];
        if (symbol >= XX_ARJ_NT) {
            uint16_t test = (uint16_t)(1U << 7U);
            do {
                if (symbol >= XX_ARJ_TREE_SIZE || test == 0U) {
                    state->error = true;
                    return false;
                }
                symbol = (state->bit_buf & test) ? state->right[symbol]
                                                 : state->left[symbol];
                test >>= 1U;
            } while (symbol >= XX_ARJ_NT);
        }
        if (symbol >= XX_ARJ_NPT ||
            !xx_arj_fill_bits(state, state->pt_len[symbol])) return false;
        if (symbol <= 2U) {
            unsigned zeroes;
            if (symbol == 0U) zeroes = 1U;
            else if (symbol == 1U) zeroes = xx_arj_get_bits(state, 4U) + 3U;
            else zeroes = xx_arj_get_bits(state, 9U) + 20U;
            if (state->error || zeroes > XX_ARJ_NC - i) {
                state->error = true;
                return false;
            }
            while (zeroes-- != 0U) state->c_len[i++] = 0U;
        } else {
            state->c_len[i++] = (uint8_t)(symbol - 2U);
        }
    }
    while (i < XX_ARJ_NC) state->c_len[i++] = 0U;
    return xx_arj_make_table(state, XX_ARJ_NC, state->c_len, 12U,
                             state->c_table, XX_ARJ_CTABLE_SIZE);
}

static uint16_t xx_arj_decode_c(xx_arj_decoder *state) {
    unsigned symbol;
    if (state->block_size == 0U) {
        state->block_size = xx_arj_get_bits(state, 16U);
        if (state->error || state->block_size == 0U ||
            !xx_arj_read_pt_lengths(state, XX_ARJ_NT, 5U, 3) ||
            !xx_arj_read_c_lengths(state) ||
            !xx_arj_read_pt_lengths(state, XX_ARJ_NP, 5U, -1)) {
            state->error = true;
            return 0U;
        }
    }
    --state->block_size;
    symbol = state->c_table[state->bit_buf >> 4U];
    if (symbol >= XX_ARJ_NC) {
        uint16_t test = (uint16_t)(1U << 3U);
        do {
            if (symbol >= XX_ARJ_TREE_SIZE || test == 0U) {
                state->error = true;
                return 0U;
            }
            symbol = (state->bit_buf & test) ? state->right[symbol]
                                             : state->left[symbol];
            test >>= 1U;
        } while (symbol >= XX_ARJ_NC);
    }
    if (!xx_arj_fill_bits(state, state->c_len[symbol])) return 0U;
    return (uint16_t)symbol;
}

static uint16_t xx_arj_decode_p(xx_arj_decoder *state) {
    unsigned symbol = state->pt_table[state->bit_buf >> 8U];
    if (symbol >= XX_ARJ_NP) {
        uint16_t test = (uint16_t)(1U << 7U);
        do {
            if (symbol >= XX_ARJ_TREE_SIZE || test == 0U) {
                state->error = true;
                return 0U;
            }
            symbol = (state->bit_buf & test) ? state->right[symbol]
                                             : state->left[symbol];
            test >>= 1U;
        } while (symbol >= XX_ARJ_NP);
    }
    if (!xx_arj_fill_bits(state, state->pt_len[symbol])) return 0U;
    if (symbol != 0U) {
        unsigned width = symbol - 1U;
        symbol = (1U << width) + xx_arj_get_bits(state, width);
    }
    return (uint16_t)symbol;
}

static uint16_t xx_arj_decode_fast_value(xx_arj_decoder *state,
                                         unsigned start,
                                         unsigned stop) {
    uint16_t value = 0U;
    uint16_t add = 0U;
    uint16_t power = (uint16_t)(1U << start);
    unsigned width;
    if (!state || state->error || state->get_len < 0 ||
        state->get_len > (int)XX_ARJ_CODE_BITS) {
        if (state) state->error = true;
        return 0U;
    }
    for (width = start; width < stop; ++width) {
        if (state->get_len <= 0) {
            state->get_buf |= state->bit_buf;
            if (!xx_arj_fill_bits(state, XX_ARJ_CODE_BITS)) return 0U;
            state->get_len = (int)XX_ARJ_CODE_BITS;
        }
        value = (state->get_buf & 0x8000U) ? 1U : 0U;
        state->get_buf = (uint16_t)(state->get_buf << 1U);
        --state->get_len;
        if (value == 0U) break;
        add = (uint16_t)(add + power);
        power = (uint16_t)(power << 1U);
    }
    if (width != 0U) {
        if (state->get_len < (int)width) {
            if (state->get_len < 0 || state->get_len > 16) {
                state->error = true;
                return 0U;
            }
            state->get_buf |= (uint16_t)(state->bit_buf >> state->get_len);
            if (!xx_arj_fill_bits(state,
                                  XX_ARJ_CODE_BITS - (unsigned)state->get_len))
                return 0U;
            state->get_len = (int)XX_ARJ_CODE_BITS;
        }
        value = (uint16_t)(state->get_buf >> (XX_ARJ_CODE_BITS - width));
        state->get_buf = (uint16_t)(state->get_buf << width);
        state->get_len -= (int)width;
    }
    return (uint16_t)(value + add);
}

static bool xx_arj_decode_compressed(uint8_t method,
                                     const uint8_t *input,
                                     size_t input_size,
                                     uint8_t *output,
                                     size_t output_size,
                                     size_t *written) {
    xx_arj_decoder state;
    uint8_t dictionary[XX_ARJ_DIC_SIZE];
    size_t produced = 0U;
    size_t dict_pos = 0U;
    bool fastest = method == 4U;

    xx_rt_memset(&state, 0, sizeof(state));
    xx_rt_memset(dictionary, 0, sizeof(dictionary));
    state.input = input;
    state.input_size = input_size;
    if (!xx_arj_fill_bits(&state, XX_ARJ_CODE_BITS)) return false;

    while (produced < output_size && !state.error) {
        unsigned token = fastest ? xx_arj_decode_fast_value(&state, 0U, 7U)
                                 : xx_arj_decode_c(&state);
        bool literal = fastest ? token == 0U : token <= 255U;
        if (state.error) break;
        if (literal) {
            uint8_t byte = (uint8_t)token;
            if (fastest) {
                if (state.get_len < 0 || state.get_len > 16) {
                    state.error = true;
                    break;
                }
                if (state.get_len < 8) {
                    state.get_buf |= (uint16_t)(state.bit_buf >> state.get_len);
                    if (!xx_arj_fill_bits(&state,
                                          XX_ARJ_CODE_BITS -
                                              (unsigned)state.get_len)) break;
                    state.get_len = (int)XX_ARJ_CODE_BITS;
                }
                byte = (uint8_t)(state.get_buf >> 8U);
                state.get_buf = (uint16_t)(state.get_buf << 8U);
                state.get_len -= 8;
            }
            output[produced++] = byte;
            dictionary[dict_pos++] = byte;
            if (dict_pos == XX_ARJ_DIC_SIZE) dict_pos = 0U;
        } else {
            unsigned match_length = fastest ? token + 2U : token - 253U;
            unsigned distance = fastest
                                    ? xx_arj_decode_fast_value(&state, 9U, 13U)
                                    : xx_arj_decode_p(&state);
            size_t source;
            unsigned i;
            if (state.error || match_length < XX_ARJ_THRESHOLD ||
                match_length > XX_ARJ_MAX_MATCH ||
                match_length > output_size - produced ||
                distance >= XX_ARJ_DIC_SIZE) {
                state.error = true;
                break;
            }
            source = dict_pos + XX_ARJ_DIC_SIZE - (size_t)distance - 1U;
            if (source >= XX_ARJ_DIC_SIZE) source -= XX_ARJ_DIC_SIZE;
            for (i = 0U; i < match_length; ++i) {
                uint8_t byte = dictionary[source++];
                if (source == XX_ARJ_DIC_SIZE) source = 0U;
                dictionary[dict_pos++] = byte;
                if (dict_pos == XX_ARJ_DIC_SIZE) dict_pos = 0U;
                output[produced++] = byte;
            }
        }
    }
    if (written) *written = produced;
    return !state.error && produced == output_size &&
           state.input_pos == input_size &&
           (fastest || state.block_size == 0U);
}

bool xx_arj_decode_memory(uint8_t method,
                          const uint8_t *input,
                          size_t input_size,
                          uint8_t *output,
                          size_t output_size,
                          size_t *written) {
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (method == 0U) {
        if (input_size != output_size) return false;
        if (output_size != 0U) xx_rt_memcpy(output, input, output_size);
        if (written) *written = output_size;
        return true;
    }
    if (method < 1U || method > 4U || output_size == 0U) return false;
    return xx_arj_decode_compressed(method, input, input_size, output,
                                    output_size, written);
}
