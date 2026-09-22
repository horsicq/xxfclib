/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HZL / JBF LZHUF decoder.  Ported one-for-one from the XArchive reference
 * decoder (XArchive/Algos/xhzldecoder.cpp), which was validated byte-exact
 * against the original extractor over the whole HZL and JBF corpora.  The
 * adaptive Huffman model, the bit reader's 16-bit look-ahead and the position
 * table are reproduced exactly; only the state lives in a caller-owned heap
 * block instead of C++ objects.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/hzl/xx_hzl.h"

#define HZL_N 8192                          /* ring size, mask 0x1fff      */
#define HZL_F 60                            /* longest match               */
#define HZL_THRESHOLD 2                     /* shortest match is THR + 1   */
#define HZL_N_CHAR (256 + HZL_F - HZL_THRESHOLD) /* 314, no stop code      */
#define HZL_T (HZL_N_CHAR * 2 - 1)          /* 627                         */
#define HZL_R (HZL_T - 1)                   /* 626                         */
#define HZL_MAX_FREQ 0x8000

/* The bit reader keeps a 16-bit look-ahead window, so a well-formed stream can
 * legitimately pull a couple of bytes past its declared end.  Anything beyond
 * this slack means the stream did not describe the requested plaintext. */
#define HZL_TAIL_SLACK 8

typedef struct hzl_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position; /* keeps advancing past the end so overrun is visible */
    uint32_t buffer;
    int32_t count;
} hzl_bits;

typedef struct hzl_state_s {
    int32_t frequency[HZL_T + 1];
    int32_t parent[HZL_T + HZL_N_CHAR];
    int32_t child[HZL_T];
    uint8_t position_code[256];
    uint8_t position_length[256];
    uint8_t ring[HZL_N + HZL_F];
} hzl_state;

static bool hzl_is_overrun(const hzl_bits *bits) {
    return bits->position > (bits->size + (size_t)HZL_TAIL_SLACK);
}

static void hzl_fill(hzl_bits *bits) {
    while (bits->count <= 8) {
        uint32_t byte = 0U;
        if (bits->position < bits->size) byte = bits->data[bits->position];
        ++bits->position;
        bits->buffer |= byte << (8 - bits->count);
        bits->count += 8;
    }
}

static int32_t hzl_read_bit(hzl_bits *bits) {
    uint32_t value;
    hzl_fill(bits);
    value = bits->buffer;
    bits->buffer = (bits->buffer << 1);
    --bits->count;
    return (int32_t)((value >> 15) & 1U);
}

static int32_t hzl_read_byte(hzl_bits *bits) {
    uint32_t value;
    hzl_fill(bits);
    value = bits->buffer;
    bits->buffer = (bits->buffer << 8);
    bits->count -= 8;
    return (int32_t)((value >> 8) & 0xffU);
}

/* Position decode table: 1/3/8/12/24/16 symbols at code lengths 3..8, each
 * occupying 1 << (8 - length) prefix slots.  Built into caller-owned state --
 * the library forbids module-level mutable tables. */
static bool hzl_build_position_table(hzl_state *state) {
    static const int32_t per_length[6] = {1, 3, 8, 12, 24, 16};
    int32_t prefix = 0;
    int32_t symbol = 0;
    int32_t length;
    for (length = 3; length <= 8; ++length) {
        int32_t span = 1 << (8 - length);
        int32_t j;
        for (j = 0; j < per_length[length - 3]; ++j) {
            int32_t k;
            for (k = 0; k < span; ++k) {
                if (prefix >= 256) return false;
                state->position_length[prefix] = (uint8_t)length;
                state->position_code[prefix] = (uint8_t)symbol;
                ++prefix;
            }
            ++symbol;
        }
    }
    return (prefix == 256) && (symbol == 64);
}

static int32_t hzl_decode_position(hzl_bits *bits, const hzl_state *state) {
    int32_t i = hzl_read_byte(bits);
    int32_t high;
    int32_t j;
    if ((i < 0) || (i > 255)) return -1;
    high = (int32_t)state->position_code[i] << 6;
    j = (int32_t)state->position_length[i] - 2;
    while (j-- > 0) {
        i = ((i << 1) + hzl_read_bit(bits)) & 0xffff;
    }
    /* Only the low 6 bits of the accumulated prefix join the table's high
     * bits, so distances never exceed 4095 even though the ring is 8 KiB.
     * That is what the reference implementation does -- deliberate. */
    return high | (i & 0x3f);
}

static void hzl_init_tree(hzl_state *state) {
    int32_t i;
    int32_t j;
    for (i = 0; i < HZL_N_CHAR; ++i) {
        state->frequency[i] = 1;
        state->child[i] = i + HZL_T;
        state->parent[i + HZL_T] = i;
    }
    i = 0;
    j = HZL_N_CHAR;
    while (j <= HZL_R) {
        state->frequency[j] = state->frequency[i] + state->frequency[i + 1];
        state->child[j] = i;
        state->parent[i] = j;
        state->parent[i + 1] = j;
        i += 2;
        ++j;
    }
    state->frequency[HZL_T] = 0xffff; /* sentinel that stops the sift loops */
    state->parent[HZL_R] = 0;
}

static void hzl_reconstruct(hzl_state *state) {
    int32_t i;
    int32_t j;
    int32_t n;
    j = 0;
    for (i = 0; i < HZL_T; ++i) {
        if (state->child[i] >= HZL_T) {
            state->frequency[j] = (state->frequency[i] + 1) / 2;
            state->child[j] = state->child[i];
            ++j;
        }
    }
    i = 0;
    j = HZL_N_CHAR;
    for (; j < HZL_T; i += 2, ++j) {
        int32_t sum = state->frequency[i] + state->frequency[i + 1];
        int32_t k = j - 1;
        int32_t m;
        while ((k > 0) && (sum < state->frequency[k])) --k;
        /* The "k > 0" stop plus this compensating ++k is the classic LZHUF
         * insertion point search; it is load-bearing, do not "simplify". */
        if (sum >= state->frequency[k]) ++k;
        for (m = j; m > k; --m) {
            state->frequency[m] = state->frequency[m - 1];
            state->child[m] = state->child[m - 1];
        }
        state->frequency[k] = sum;
        state->child[k] = i;
    }
    for (n = 0; n < HZL_T; ++n) {
        int32_t k = state->child[n];
        state->parent[k] = n;
        if (k < HZL_T) state->parent[k + 1] = n;
    }
}

static bool hzl_update(hzl_state *state, int32_t character) {
    int32_t c;
    if (state->frequency[HZL_R] == HZL_MAX_FREQ) hzl_reconstruct(state);
    c = state->parent[character + HZL_T];
    do {
        int32_t k;
        int32_t l;
        if ((c < 0) || (c >= HZL_T)) return false;
        k = ++state->frequency[c];
        l = c + 1;
        if (k > state->frequency[l]) {
            /* frequency[HZL_T] is the 0xffff sentinel, so this walk always
             * terminates at or before HZL_T on a sane tree; the bound is
             * defensive only. */
            while ((l < HZL_T) && (k > state->frequency[l])) ++l;
            if (k > state->frequency[l]) return false;
            --l;
            if ((l < 0) || (l >= HZL_T)) return false;
            {
                int32_t i = state->child[c];
                int32_t j;
                state->frequency[c] = state->frequency[l];
                state->frequency[l] = k;
                if ((i < 0) || (i >= HZL_T + HZL_N_CHAR)) return false;
                state->parent[i] = l;
                if (i < HZL_T) state->parent[i + 1] = l;
                j = state->child[l];
                state->child[l] = i;
                if ((j < 0) || (j >= HZL_T + HZL_N_CHAR)) return false;
                state->parent[j] = c;
                if (j < HZL_T) state->parent[j + 1] = c;
                state->child[c] = j;
                c = l;
            }
        }
        c = state->parent[c];
    } while (c != 0);
    return true;
}

static int32_t hzl_decode_character(hzl_state *state, hzl_bits *bits) {
    int32_t code = state->child[HZL_R];
    int32_t guard = 0;
    while (code < HZL_T) {
        if (++guard > (HZL_T * 2)) return -1;
        code += hzl_read_bit(bits);
        if ((code < 0) || (code >= HZL_T)) return -1;
        code = state->child[code];
    }
    code -= HZL_T;
    if ((code < 0) || (code >= HZL_N_CHAR)) return -1;
    if (!hzl_update(state, code)) return -1;
    return code;
}

bool xx_hzl_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    hzl_state *state;
    hzl_bits bits;
    size_t produced = 0U;
    uint32_t ring_position = 0U;
    const uint32_t mask = (uint32_t)HZL_N - 1U;
    bool ok = false;

    if (written) *written = 0U;
    if (output_size == 0U) return true;
    if (!input || !output || (input_size == 0U)) return false;

    state = (hzl_state *)xx_mem_alloc(sizeof(hzl_state));
    if (!state) return false;
    xx_rt_memset(state, 0, sizeof(*state));
    if (!hzl_build_position_table(state)) {
        xx_mem_free(state);
        return false;
    }
    hzl_init_tree(state);
    /* The ring is prefilled with spaces, so an early back-reference that
     * points "before" the output is legitimate and yields 0x20 -- this is the
     * format's defined initial window, not an out-of-range access. */
    xx_rt_memset(state->ring, 0x20, sizeof(state->ring));

    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.buffer = 0U;
    bits.count = 0;

    while (produced < output_size) {
        int32_t character = hzl_decode_character(state, &bits);
        if ((character < 0) || hzl_is_overrun(&bits)) goto done;
        if (character < 256) {
            output[produced++] = (uint8_t)character;
            state->ring[ring_position] = (uint8_t)character;
            ring_position = (ring_position + 1U) & mask;
            continue;
        }
        {
            int32_t position = hzl_decode_position(&bits, state);
            int32_t length = character + HZL_THRESHOLD - 0xff;
            uint32_t source;
            int32_t i;
            if ((position < 0) || hzl_is_overrun(&bits)) goto done;
            if ((length < 3) || (length > HZL_F + HZL_THRESHOLD)) goto done;
            /* The reference clamps a final overlong match to the remaining
             * plaintext instead of rejecting the member; the stream has no
             * stop code, so the last match legitimately overshoots.
             * Deliberate -- do not turn this into an error. */
            if ((size_t)length > (output_size - produced)) {
                length = (int32_t)(output_size - produced);
            }
            source = ((ring_position - (uint32_t)position) - 1U) & mask;
            for (i = 0; i < length; ++i) {
                uint8_t value = state->ring[source & mask];
                output[produced++] = value;
                state->ring[ring_position] = value;
                ring_position = (ring_position + 1U) & mask;
                source = (source & mask) + 1U;
            }
        }
    }

    ok = (produced == output_size);

done:
    xx_mem_free(state);
    if (ok) {
        if (written) *written = produced;
        return true;
    }
    if (written) *written = 0U;
    return false;
}
