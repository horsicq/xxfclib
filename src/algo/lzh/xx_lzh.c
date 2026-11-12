/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LArc/LHA -lh1-: 4 KiB LZSS with adaptive Huffman coding.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <string.h>

#define XX_LH1_N 4096
#define XX_LH1_F 60
#define XX_LH1_THRESHOLD 2
#define XX_LH1_N_CHAR (256 - XX_LH1_THRESHOLD + XX_LH1_F)
#define XX_LH1_T (XX_LH1_N_CHAR * 2 - 1)
#define XX_LH1_R (XX_LH1_T - 1)
#define XX_LH1_MAX_FREQ 0x8000

typedef struct xx_lh1_state_s {
    const uint8_t *input;
    size_t input_size;
    size_t bit_pos;
    xx_io_device *device;
    uint64_t stream_size, stream_bits, stream_read;
    uint8_t input_buffer[4096];
    size_t input_pos, input_count;
    uint8_t stream_byte;
    bool error;
    int freq[XX_LH1_T + 1];
    int parent[XX_LH1_T + XX_LH1_N_CHAR];
    int child[XX_LH1_T];
    uint8_t position_length[256];
    uint8_t position_code[256];
    uint8_t dictionary[XX_LH1_N];
} xx_lh1_state;

static unsigned xx_lh1_bits(xx_lh1_state *state, unsigned count)
{
    unsigned result = 0U;
    unsigned i;
    if (!state || state->error || count > 16U ||
        (state->device ? (state->stream_bits > state->stream_size * 8U || count > state->stream_size * 8U - state->stream_bits)
                       : (state->input_size > SIZE_MAX / 8U || state->bit_pos > state->input_size * 8U || count > state->input_size * 8U - state->bit_pos))) {
        if (state) state->error = true;
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        if (state->device) {
            if (!(state->stream_bits & 7U)) {
                if (state->input_pos == state->input_count) {
                    uint64_t remaining = state->stream_size - state->stream_read;
                    size_t wanted = remaining < sizeof(state->input_buffer) ? (size_t)remaining : sizeof(state->input_buffer);
                    ssize_t received = xx_io_read(state->device, state->input_buffer, wanted);
                    if (received <= 0 || (size_t)received > wanted) {
                        state->error = true;
                        return 0U;
                    }
                    state->input_pos = 0U;
                    state->input_count = (size_t)received;
                    state->stream_read += (size_t)received;
                }
                state->stream_byte = state->input_buffer[state->input_pos++];
            }
            result = (result << 1U) | ((state->stream_byte >> (7U - (state->stream_bits & 7U))) & 1U);
            ++state->stream_bits;
            continue;
        }
        result = (result << 1U) | ((state->input[state->bit_pos >> 3U] >> (7U - (state->bit_pos & 7U))) & 1U);
        ++state->bit_pos;
    }
    return result;
}

static void xx_lh1_init(xx_lh1_state *state)
{
    static const int counts[6] = {1, 3, 8, 12, 24, 16};
    int length, symbol = 0, table = 0;
    xx_rt_memset(state, 0, sizeof(*state));
    xx_rt_memset(state->dictionary, ' ', XX_LH1_N - XX_LH1_F);
    for (length = 3; length <= 8; ++length) {
        int i, j, span = 1 << (8 - length);
        for (i = 0; i < counts[length - 3]; ++i) {
            for (j = 0; j < span; ++j) {
                state->position_length[table] = (uint8_t)length;
                state->position_code[table++] = (uint8_t)symbol;
            }
            ++symbol;
        }
    }
}

static void xx_lh1_start_tree(xx_lh1_state *state)
{
    int i;
    int j;
    for (i = 0; i < XX_LH1_N_CHAR; ++i) {
        state->freq[i] = 1;
        state->child[i] = i + XX_LH1_T;
        state->parent[i + XX_LH1_T] = i;
    }
    i = 0;
    j = XX_LH1_N_CHAR;
    while (j <= XX_LH1_R) {
        state->freq[j] = state->freq[i] + state->freq[i + 1];
        state->child[j] = i;
        state->parent[i] = j;
        state->parent[i + 1] = j;
        i += 2;
        ++j;
    }
    state->freq[XX_LH1_T] = 0xffff;
    state->parent[XX_LH1_R] = 0;
}

#include "xx_sort_algo_lzh_model.inc"

static int xx_lh1_decode_symbol(xx_lh1_state *state)
{
    int symbol = state->child[XX_LH1_R];
    while (symbol < XX_LH1_T && !state->error) {
        symbol += (int)xx_lh1_bits(state, 1U);
        if (!state->error) symbol = state->child[symbol];
    }
    if (state->error) return 0;
    symbol -= XX_LH1_T;
    if (symbol < 0 || symbol >= XX_LH1_N_CHAR) {
        state->error = true;
        return 0;
    }
    xx_lh1_update(state, symbol);
    return symbol;
}

static unsigned xx_lh1_decode_position(xx_lh1_state *state)
{
    unsigned value = xx_lh1_bits(state, 8U);
    unsigned position = (unsigned)state->position_code[value] << 6U;
    int remaining = (int)state->position_length[value] - 2;
    while (remaining-- > 0 && !state->error) value = (value << 1U) + xx_lh1_bits(state, 1U);
    return position | (value & 0x3fU);
}

static inline unsigned xx_lh1_read_bits_mem(xx_lh1_state *state, unsigned count)
{
    if (state->bit_pos + count > state->input_size * 8U) {
        state->error = true;
        return 0U;
    }
    if (count == 0) return 0U;
    size_t byte_pos = state->bit_pos >> 3U;
    unsigned bit_shift = (unsigned)(state->bit_pos & 7U);
    state->bit_pos += count;
    unsigned w = (unsigned)state->input[byte_pos];
    if (bit_shift + count > 8U) {
        w = (w << 8U) | state->input[byte_pos + 1];
        return (w >> (16U - bit_shift - count)) & ((1U << count) - 1U);
    }
    return (w >> (8U - bit_shift - count)) & ((1U << count) - 1U);
}

static inline int xx_lh1_decode_symbol_mem(xx_lh1_state *state)
{
    int symbol = state->child[XX_LH1_R];
    const int *child = state->child;
    const uint8_t *input = state->input;
    size_t bit_pos = state->bit_pos;
    size_t max_bits = state->input_size * 8U;

    while (symbol < XX_LH1_T) {
        if (bit_pos >= max_bits) {
            state->bit_pos = bit_pos;
            state->error = true;
            return 0;
        }
        unsigned bit = (input[bit_pos >> 3U] >> (7U - (bit_pos & 7U))) & 1U;
        bit_pos++;
        symbol = child[symbol + (int)bit];
    }
    state->bit_pos = bit_pos;
    symbol -= XX_LH1_T;
    if (symbol < 0 || symbol >= XX_LH1_N_CHAR) {
        state->error = true;
        return 0;
    }
    xx_lh1_update(state, symbol);
    return symbol;
}

static inline unsigned xx_lh1_decode_position_mem(xx_lh1_state *state)
{
    unsigned value = xx_lh1_read_bits_mem(state, 8U);
    unsigned position = (unsigned)state->position_code[value] << 6U;
    int remaining = (int)state->position_length[value] - 2;
    if (remaining > 0 && !state->error) {
        value = (value << remaining) | xx_lh1_read_bits_mem(state, (unsigned)remaining);
    }
    return position | (value & 0x3fU);
}

bool xx_lzh1_decode_memory_ext(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written, size_t *consumed_bytes)
{
    static const int symbol_counts[6] = {1, 3, 8, 12, 24, 16};
    xx_lh1_state state;
    size_t output_pos = 0U;
    unsigned dictionary_pos = XX_LH1_N - XX_LH1_F;
    int table_index = 0;
    int symbol_index = 0;
    int length;
    if (consumed_bytes) *consumed_bytes = 0U;
    if (written) *written = 0U;
    if (input_size > SIZE_MAX / 8U || (!input && input_size != 0U) || (!output && output_size != 0U) || (output_size != 0U && input_size == 0U)) return false;
    xx_rt_memset(&state, 0, sizeof(state));
    state.input = input;
    state.input_size = input_size;
    xx_rt_memset(state.dictionary, ' ', XX_LH1_N - XX_LH1_F);
    for (length = 3; length <= 8; ++length) {
        int span = 1 << (8 - length);
        int i;
        for (i = 0; i < symbol_counts[length - 3]; ++i) {
            int j;
            for (j = 0; j < span; ++j) {
                state.position_length[table_index] = (uint8_t)length;
                state.position_code[table_index] = (uint8_t)symbol_index;
                ++table_index;
            }
            ++symbol_index;
        }
    }
    if (table_index != 256 || symbol_index != 64) return false;
    xx_lh1_start_tree(&state);
    while (output_pos < output_size && !state.error) {
        int symbol = xx_lh1_decode_symbol_mem(&state);
        if (state.error) break;
        if (symbol < 256) {
            uint8_t byte = (uint8_t)symbol;
            output[output_pos++] = byte;
            state.dictionary[dictionary_pos++] = byte;
            dictionary_pos &= XX_LH1_N - 1U;
        } else {
            unsigned distance = xx_lh1_decode_position_mem(&state);
            unsigned source = (dictionary_pos - distance - 1U) & (XX_LH1_N - 1U);
            unsigned count = (unsigned)(symbol - 255 + XX_LH1_THRESHOLD);
            if (state.error || count > output_size - output_pos) {
                state.error = true;
                break;
            }
            if (source + count <= XX_LH1_N && dictionary_pos + count <= XX_LH1_N && distance >= count &&
                (source + count <= dictionary_pos || dictionary_pos + count <= source)) {
                xx_rt_memcpy(&output[output_pos], &state.dictionary[source], count);
                xx_rt_memcpy(&state.dictionary[dictionary_pos], &state.dictionary[source], count);
                output_pos += count;
                dictionary_pos += count;
                dictionary_pos &= XX_LH1_N - 1U;
            } else if (source + count <= XX_LH1_N && dictionary_pos + count <= XX_LH1_N && distance == 0) {
                uint8_t byte = state.dictionary[source];
                xx_rt_memset(&output[output_pos], byte, count);
                xx_rt_memset(&state.dictionary[dictionary_pos], byte, count);
                output_pos += count;
                dictionary_pos += count;
                dictionary_pos &= XX_LH1_N - 1U;
            } else {
                for (unsigned i = 0U; i < count; ++i) {
                    uint8_t byte = state.dictionary[(source + i) & (XX_LH1_N - 1U)];
                    output[output_pos++] = byte;
                    state.dictionary[dictionary_pos++] = byte;
                    dictionary_pos &= XX_LH1_N - 1U;
                }
            }
        }
    }
    if (written) *written = output_pos;
    if (state.error || output_pos != output_size || state.bit_pos > input_size * 8U) return false;
    if (consumed_bytes) {
        *consumed_bytes = (size_t)((state.bit_pos + 7U) / 8U);
        return true;
    }
    {
        size_t unused_bits = input_size * 8U - state.bit_pos;

        /* Some LHArc SFX writers count one final zero byte in the member's
         * packed size when the Huffman stream ends exactly on a byte boundary.
         * The declared output size still terminates decoding; accept only
         * that single zero byte, never arbitrary trailing payload. */
        return unused_bits < 8U || (unused_bits == 8U && input[input_size - 1U] == 0U);
    }
}

bool xx_lzh1_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written)
{
    return xx_lzh1_decode_memory_ext(input, input_size, output, output_size, written, NULL);
}

bool xx_lzh1_decode_to_device(xx_io_device *input, uint64_t input_size, xx_io_device *output, uint64_t output_size, uint64_t *written, xx_pd_struct *pd)
{
    xx_lh1_state state;
    uint8_t buffer[4096];
    uint64_t done = 0, delivered = 0, unused;
    size_t used = 0;
    unsigned position = XX_LH1_N - XX_LH1_F;
    int level = -1;
    bool ok = false;
    if (written) *written = 0;
    if (!input || !output || input_size > UINT64_MAX / 8U || (output_size && !input_size) || xx_pd_is_stopped(pd)) return false;
    xx_lh1_init(&state);
    xx_lh1_start_tree(&state);
    state.device = input;
    state.stream_size = input_size;
    level = xx_pd_enter_level(pd, output_size, "Decoding LH1 member");
    while (done < output_size && !state.error) {
        int symbol = xx_lh1_decode_symbol(&state);
        unsigned count = 1, source = 0, i;
        if (state.error) break;
        if (symbol >= 256) {
            unsigned distance = xx_lh1_decode_position(&state);
            source = (position - distance - 1U) & (XX_LH1_N - 1U);
            count = (unsigned)(symbol - 255 + XX_LH1_THRESHOLD);
        }
        if (state.error || count > output_size - done) break;
        for (i = 0; i < count; ++i) {
            uint8_t byte = symbol < 256 ? (uint8_t)symbol : state.dictionary[(source + i) & (XX_LH1_N - 1U)];
            buffer[used++] = byte;
            ++done;
            state.dictionary[position++] = byte;
            position &= XX_LH1_N - 1U;
            if (used == sizeof(buffer) || done == output_size) {
                size_t sent = 0;
                if (xx_pd_is_stopped(pd)) goto cleanup;
                while (sent < used) {
                    if (xx_pd_is_stopped(pd)) goto cleanup;
                    ssize_t n = xx_io_write(output, buffer + sent, used - sent);
                    if (n <= 0 || (size_t)n > used - sent) goto cleanup;
                    sent += (size_t)n;
                    delivered += (size_t)n;
                }
                used = 0;
                xx_pd_set_current(pd, level, done);
            }
        }
    }
    if (state.error || done != output_size || xx_pd_is_stopped(pd)) goto cleanup;
    unused = input_size * 8U - state.stream_bits;
    if (unused < 8U) ok = true;
    else if (unused == 8U) ok = xx_lh1_bits(&state, 8U) == 0U && !state.error;
cleanup:
    if (written) *written = delivered;
    if (level >= 0) xx_pd_leave_level(pd, level);
    return ok && !xx_pd_is_stopped(pd);
}
