/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AMPK's two original Okumura-family codecs.  The arithmetic variant uses an
 * adaptive model and intentionally supplies zero bits after the byte stream:
 * legacy encoders rely on that look-ahead while initializing their interval.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/ampk/xx_ampk.h"

#include "xxfclib/memory/xx_memory.h"

#include <string.h>

#define AMPK_RING_SIZE 4096U
#define AMPK_RING_MASK (AMPK_RING_SIZE - 1U)
#define AMPK_LZSS_F 18U
#define AMPK_LZARI_F 60U
#define AMPK_THRESHOLD 2U
#define AMPK_LZARI_NCHAR (256U - AMPK_THRESHOLD + AMPK_LZARI_F)
#define AMPK_LZARI_M 15U
#define AMPK_Q1 (UINT32_C(1) << AMPK_LZARI_M)
#define AMPK_Q2 (UINT32_C(2) * AMPK_Q1)
#define AMPK_Q3 (UINT32_C(3) * AMPK_Q1)
#define AMPK_Q4 (UINT32_C(4) * AMPK_Q1)
#define AMPK_MAX_CUM (AMPK_Q1 - UINT32_C(1))
#define AMPK_MAX_OUTPUT ((size_t)UINT32_C(0x10000000))

typedef struct ampk_bits_s {
    const uint8_t *input;
    size_t input_size;
    size_t bit_position;
    bool overflow;
} ampk_bits;

typedef struct ampk_lzari_s {
    ampk_bits bits;
    bool error;
    uint32_t low;
    uint32_t high;
    uint32_t value;
    uint32_t symbol_frequency[AMPK_LZARI_NCHAR + 1U];
    uint32_t symbol_cumulative[AMPK_LZARI_NCHAR + 1U];
    uint32_t position_cumulative[AMPK_RING_SIZE + 1U];
    int32_t char_to_symbol[AMPK_LZARI_NCHAR];
    int32_t symbol_to_char[AMPK_LZARI_NCHAR + 1U];
    uint8_t ring[AMPK_RING_SIZE];
} ampk_lzari;

static uint32_t ampk_get_bit(ampk_bits *bits) {
    size_t byte_position;
    uint32_t result = 0U;
    if (!bits || bits->overflow) return 0U;
    byte_position = bits->bit_position >> 3U;
    if (byte_position < bits->input_size)
        result = (uint32_t)((bits->input[byte_position] >>
                             (7U - (bits->bit_position & 7U))) & 1U);
    if (bits->bit_position == SIZE_MAX) {
        bits->overflow = true;
        return 0U;
    }
    ++bits->bit_position;
    return result;
}

static size_t ampk_consumed_size(const ampk_bits *bits) {
    if (!bits) return SIZE_MAX;
    return (bits->bit_position >> 3U) +
           ((bits->bit_position & 7U) != 0U ? 1U : 0U);
}

static void ampk_lzari_start(ampk_lzari *state, const uint8_t *input,
                             size_t input_size) {
    unsigned index;
    state->bits.input = input;
    state->bits.input_size = input_size;
    state->low = 0U;
    state->high = AMPK_Q4;
    state->symbol_cumulative[AMPK_LZARI_NCHAR] = 0U;
    for (index = AMPK_LZARI_NCHAR; index > 0U; --index) {
        unsigned character = index - 1U;
        state->char_to_symbol[character] = (int32_t)index;
        state->symbol_to_char[index] = (int32_t)character;
        state->symbol_frequency[index] = 1U;
        state->symbol_cumulative[index - 1U] =
            state->symbol_cumulative[index] + state->symbol_frequency[index];
    }
    state->symbol_frequency[0] = 0U;
    state->symbol_to_char[0] = 0;
    state->position_cumulative[AMPK_RING_SIZE] = 0U;
    for (index = AMPK_RING_SIZE; index > 0U; --index)
        state->position_cumulative[index - 1U] =
            state->position_cumulative[index] + 10000U / (index + 200U);
    xx_rt_memset(state->ring, ' ', sizeof(state->ring));
    for (index = 0U; index < AMPK_LZARI_M + 2U; ++index)
        state->value = (state->value << 1U) + ampk_get_bit(&state->bits);
}

static int ampk_search_symbol(const ampk_lzari *state, uint32_t target) {
    int first = 1;
    int last = (int)AMPK_LZARI_NCHAR;
    while (first < last) {
        int middle = (first + last) / 2;
        if (state->symbol_cumulative[middle] > target)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

static int ampk_search_position(const ampk_lzari *state, uint32_t target) {
    int first = 1;
    int last = (int)AMPK_RING_SIZE;
    while (first < last) {
        int middle = (first + last) / 2;
        if (state->position_cumulative[middle] > target)
            first = middle + 1;
        else
            last = middle;
    }
    return first - 1;
}

static bool ampk_lzari_renormalize(ampk_lzari *state) {
    unsigned guard;
    for (guard = 0U; guard < 256U; ++guard) {
        if (state->low >= AMPK_Q2) {
            state->value -= AMPK_Q2;
            state->low -= AMPK_Q2;
            state->high -= AMPK_Q2;
        } else if (state->low >= AMPK_Q1 && state->high <= AMPK_Q3) {
            state->value -= AMPK_Q1;
            state->low -= AMPK_Q1;
            state->high -= AMPK_Q1;
        } else if (state->high > AMPK_Q2) {
            return !state->bits.overflow;
        }
        state->low += state->low;
        state->high += state->high;
        state->value = (state->value << 1U) + ampk_get_bit(&state->bits);
    }
    return false;
}

static void ampk_lzari_update(ampk_lzari *state, int symbol) {
    int index;
    if (state->symbol_cumulative[0] >= AMPK_MAX_CUM) {
        uint32_t accumulated = 0U;
        for (index = (int)AMPK_LZARI_NCHAR; index > 0; --index) {
            state->symbol_cumulative[index] = accumulated;
            state->symbol_frequency[index] =
                (state->symbol_frequency[index] + 1U) >> 1U;
            accumulated += state->symbol_frequency[index];
        }
        state->symbol_cumulative[0] = accumulated;
    }
    index = symbol;
    while (index > 0 && state->symbol_frequency[index] ==
                            state->symbol_frequency[index - 1])
        --index;
    if (index < symbol) {
        int first = state->symbol_to_char[index];
        int second = state->symbol_to_char[symbol];
        state->symbol_to_char[index] = second;
        state->symbol_to_char[symbol] = first;
        state->char_to_symbol[first] = symbol;
        state->char_to_symbol[second] = index;
    }
    ++state->symbol_frequency[index];
    for (--index; index >= 0; --index) ++state->symbol_cumulative[index];
}

static int ampk_lzari_decode_char(ampk_lzari *state) {
    uint32_t range;
    uint64_t target;
    int symbol;
    if (!state || state->value < state->low || state->value >= state->high ||
        (range = state->high - state->low) == 0U ||
        state->symbol_cumulative[0] == 0U) return -1;
    target = (((uint64_t)(state->value - state->low + 1U) *
               state->symbol_cumulative[0]) - 1U) / range;
    if (target >= state->symbol_cumulative[0]) return -1;
    symbol = ampk_search_symbol(state, (uint32_t)target);
    if (symbol < 1 || symbol > (int)AMPK_LZARI_NCHAR) return -1;
    state->high = state->low + (uint32_t)(((uint64_t)range *
                                            state->symbol_cumulative[symbol - 1]) /
                                           state->symbol_cumulative[0]);
    state->low += (uint32_t)(((uint64_t)range *
                              state->symbol_cumulative[symbol]) /
                             state->symbol_cumulative[0]);
    if (!ampk_lzari_renormalize(state)) return -1;
    {
        int character = state->symbol_to_char[symbol];
        ampk_lzari_update(state, symbol);
        return character;
    }
}

static int ampk_lzari_decode_position(ampk_lzari *state) {
    uint32_t range;
    uint64_t target;
    int position;
    if (!state || state->value < state->low || state->value >= state->high ||
        (range = state->high - state->low) == 0U ||
        state->position_cumulative[0] == 0U) return -1;
    target = (((uint64_t)(state->value - state->low + 1U) *
               state->position_cumulative[0]) - 1U) / range;
    if (target >= state->position_cumulative[0]) return -1;
    position = ampk_search_position(state, (uint32_t)target);
    if (position < 0 || position >= (int)AMPK_RING_SIZE) return -1;
    state->high = state->low + (uint32_t)(((uint64_t)range *
                                            state->position_cumulative[position]) /
                                           state->position_cumulative[0]);
    state->low += (uint32_t)(((uint64_t)range *
                              state->position_cumulative[position + 1]) /
                             state->position_cumulative[0]);
    return ampk_lzari_renormalize(state) ? position : -1;
}

bool xx_ampk_lzss_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written) {
    uint8_t ring[AMPK_RING_SIZE];
    size_t input_position = 0U, output_position = 0U;
    unsigned ring_position = AMPK_RING_SIZE - AMPK_LZSS_F;
    uint32_t flags = 0U;
    unsigned flag_bits = 0U;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        output_size > AMPK_MAX_OUTPUT) return false;
    if (output_size == 0U) return true;
    if (input_size == 0U) return false;
    xx_rt_memset(ring, ' ', sizeof(ring));
    xx_rt_memset(ring + AMPK_RING_SIZE - AMPK_LZSS_F, 0, AMPK_LZSS_F);
    while (output_position < output_size) {
        if (flag_bits == 0U) {
            if (input_position >= input_size) return false;
            flags = input[input_position++];
            flag_bits = 8U;
        }
        if ((flags & 1U) != 0U) {
            uint8_t byte;
            if (input_position >= input_size) return false;
            byte = input[input_position++];
            output[output_position++] = byte;
            ring[ring_position] = byte;
            ring_position = (ring_position + 1U) & AMPK_RING_MASK;
        } else {
            uint8_t first, second;
            unsigned source, length, index;
            if (input_size - input_position < 2U) return false;
            first = input[input_position++];
            second = input[input_position++];
            source = (unsigned)first | ((unsigned)(second & 0xf0U) << 4U);
            length = (unsigned)(second & 0x0fU) + 3U;
            for (index = 0U; index < length && output_position < output_size;
                 ++index) {
                uint8_t byte = ring[(source + index) & AMPK_RING_MASK];
                output[output_position++] = byte;
                ring[ring_position] = byte;
                ring_position = (ring_position + 1U) & AMPK_RING_MASK;
            }
        }
        flags >>= 1U;
        --flag_bits;
    }
    if (written) *written = output_position;
    return true;
}

bool xx_ampk_lzari_decode_memory(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size,
                                 size_t *written) {
    ampk_lzari *state;
    size_t output_position = 0U;
    unsigned ring_position = AMPK_RING_SIZE - AMPK_LZARI_F;
    bool result = false;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        output_size > AMPK_MAX_OUTPUT) return false;
    if (output_size == 0U) return true;
    if (input_size == 0U) return false;
    state = (ampk_lzari *)xx_mem_calloc(1U, sizeof(*state));
    if (!state) return false;
    ampk_lzari_start(state, input, input_size);
    while (output_position < output_size && !state->bits.overflow) {
        size_t consumed = ampk_consumed_size(&state->bits);
        int character;
        if (consumed > input_size && consumed - input_size > 8U) goto done;
        character = ampk_lzari_decode_char(state);
        if (character < 0 || character >= (int)AMPK_LZARI_NCHAR) goto done;
        if (character < 256) {
            uint8_t byte = (uint8_t)character;
            output[output_position++] = byte;
            state->ring[ring_position] = byte;
            ring_position = (ring_position + 1U) & AMPK_RING_MASK;
        } else {
            int position = ampk_lzari_decode_position(state);
            unsigned source, length, index;
            if (position < 0) goto done;
            source = (ring_position - (unsigned)position - 1U) & AMPK_RING_MASK;
            length = (unsigned)character - 255U + AMPK_THRESHOLD;
            for (index = 0U; index < length && output_position < output_size;
                 ++index) {
                uint8_t byte = state->ring[(source + index) & AMPK_RING_MASK];
                output[output_position++] = byte;
                state->ring[ring_position] = byte;
                ring_position = (ring_position + 1U) & AMPK_RING_MASK;
            }
        }
    }
    result = !state->bits.overflow && output_position == output_size;
done:
    if (written) *written = output_position;
    xx_mem_free(state);
    return result;
}
