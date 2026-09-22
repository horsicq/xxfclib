/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SLS LZHUF decoder.  Ported one-for-one from the XArchive reference decoder
 * (XArchive/Algos/xslsdecoder.cpp), which was validated byte-exact against the
 * original extractor over the whole SLS corpus.  The adaptive Huffman model,
 * the bit reader's EOF behaviour and the 13-bit position code are reproduced
 * exactly; only the state lives in a caller-owned heap block instead of C++
 * objects.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/sls/xx_sls.h"

#define SLS_N 8192                               /* ring size, mask 0x1fff   */
#define SLS_N_MASK (SLS_N - 1)
#define SLS_F 90                                 /* longest match            */
#define SLS_THRESHOLD 2                          /* shortest match is THR+1  */
#define SLS_N_CHAR (256 - SLS_THRESHOLD + SLS_F) /* 344, no stop code        */
#define SLS_T (SLS_N_CHAR * 2 - 1)               /* 687                      */
#define SLS_R (SLS_T - 1)                        /* 686                      */
#define SLS_MAX_FREQ 0x8000U

/* Stock Okumura position tables, byte-identical to the reference's copies.
 * Read-only, so they are not "mutable module state". */
static const uint8_t xx_sls_d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d,
    0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1a, 0x1a, 0x1b, 0x1b,
    0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 0x1e, 0x1f, 0x1f,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2a, 0x2a, 0x2b, 0x2b,
    0x2c, 0x2c, 0x2d, 0x2d, 0x2e, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

static const uint8_t xx_sls_d_len[16] = {
    3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8
};

typedef struct sls_state_s {
    uint32_t frequency[SLS_T + 1]; /* [T] is the 0xffff sift sentinel        */
    /* One slot past T in child[]: a corrupt tree can make the walk in
     * sls_decode_character() index child[T], which a well-formed tree never
     * does.  The reference sizes it the same way, for the same reason. */
    int32_t child[SLS_T + 1];
    int32_t parent[SLS_T + SLS_N_CHAR];
    uint8_t ring[SLS_N + SLS_F - 1];
} sls_state;

typedef struct sls_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t buffer;
    int32_t count;
    bool eof;
} sls_bits;

/* The reference's fill() always contributes eight bits, even the first time it
 * runs off the end -- it just latches eof.  Every caller then rejects the
 * symbol it was in the middle of, so those phantom zero bits never reach the
 * output.  Reproduced exactly: a stricter "stop at eof" would change which
 * streams decode. */
static void sls_fill(sls_bits *bits) {
    uint32_t byte = 0U;
    if (bits->position < bits->size) {
        byte = bits->data[bits->position];
        ++bits->position;
    } else {
        bits->eof = true;
    }
    bits->buffer = bits->buffer | (byte << (8 - bits->count));
    bits->count += 8;
}

static int32_t sls_read_bit(sls_bits *bits) {
    uint32_t value;
    while (bits->count == 0) {
        if (bits->eof) return 0;
        sls_fill(bits);
    }
    value = bits->buffer;
    bits->buffer = (bits->buffer << 1);
    --bits->count;
    return (value & 0x8000U) ? 1 : 0;
}

static int32_t sls_read_byte(sls_bits *bits) {
    uint32_t value;
    while (bits->count < 8) {
        if (bits->eof) return 0;
        sls_fill(bits);
    }
    value = bits->buffer;
    bits->buffer = (bits->buffer << 8);
    bits->count -= 8;
    return (int32_t)((value >> 8) & 0xffU);
}

static void sls_init_tree(sls_state *state) {
    int32_t i;
    int32_t j;
    for (i = 0; i < SLS_N_CHAR; ++i) {
        state->frequency[i] = 1U;
        state->child[i] = i + SLS_T;
        state->parent[i + SLS_T] = i;
    }
    i = 0;
    j = SLS_N_CHAR;
    while (j <= SLS_R) {
        state->frequency[j] = state->frequency[i] + state->frequency[i + 1];
        state->child[j] = i;
        state->parent[i] = j;
        state->parent[i + 1] = j;
        i += 2;
        ++j;
    }
    state->frequency[SLS_T] = 0xffffU; /* sentinel that stops the sift loops */
    state->parent[SLS_R] = 0;
}

static void sls_reconstruct(sls_state *state) {
    int32_t i;
    int32_t j;
    int32_t n;
    j = 0;
    for (i = 0; i < SLS_T; ++i) {
        if (state->child[i] >= SLS_T) {
            state->frequency[j] = (state->frequency[i] + 1U) / 2U;
            state->child[j] = state->child[i];
            ++j;
        }
    }
    i = 0;
    for (j = SLS_N_CHAR; j < SLS_T; ++j) {
        uint32_t sum = state->frequency[i] + state->frequency[i + 1];
        int32_t k = j - 1;
        int32_t n2;
        /* The reference's insertion search has no lower bound and then always
         * increments: the halved leaf frequencies are sorted ascending and are
         * all >= 1, so sum >= 2 >= frequency[0] and the walk can never run off
         * the front.  Kept exactly as the reference spells it. */
        while (sum < state->frequency[k]) --k;
        ++k;
        for (n2 = j; n2 > k; --n2) {
            state->frequency[n2] = state->frequency[n2 - 1];
            state->child[n2] = state->child[n2 - 1];
        }
        state->frequency[k] = sum;
        state->child[k] = i;
        i += 2;
    }
    for (n = 0; n < SLS_T; ++n) {
        int32_t k = state->child[n];
        state->parent[k] = n;
        if (k < SLS_T) state->parent[k + 1] = n;
    }
}

static bool sls_update(sls_state *state, int32_t character) {
    int32_t c;
    if (state->frequency[SLS_R] == SLS_MAX_FREQ) sls_reconstruct(state);
    c = state->parent[character + SLS_T];
    do {
        uint32_t k;
        if ((c < 0) || (c >= SLS_T)) return false; /* defensive only */
        ++state->frequency[c];
        k = state->frequency[c];
        if (k > state->frequency[c + 1]) {
            int32_t l = c + 2;
            int32_t i;
            int32_t j;
            /* frequency[T] is the 0xffff sentinel and k <= MAX_FREQ (0x8000),
             * so this walk always stops at or before T on a sane tree; the
             * bound is defensive. */
            while ((l <= SLS_T) && (k > state->frequency[l])) ++l;
            if ((l > SLS_T) || (k > state->frequency[l])) return false;
            --l;
            if ((l < 0) || (l >= SLS_T)) return false;
            state->frequency[c] = state->frequency[l];
            state->frequency[l] = k;
            i = state->child[c];
            if ((i < 0) || (i >= SLS_T + SLS_N_CHAR)) return false;
            state->parent[i] = l;
            if (i < SLS_T) state->parent[i + 1] = l;
            j = state->child[l];
            if ((j < 0) || (j >= SLS_T + SLS_N_CHAR)) return false;
            state->child[l] = i;
            state->parent[j] = c;
            if (j < SLS_T) state->parent[j + 1] = c;
            state->child[c] = j;
            c = l;
        }
        c = state->parent[c];
    } while (c != 0);
    return true;
}

static int32_t sls_decode_character(sls_state *state, sls_bits *bits) {
    int32_t code = state->child[SLS_R];
    int32_t depth = 0;
    while (code < SLS_T) {
        if ((code < 0) || (++depth > SLS_T)) return -1;
        code = state->child[code + sls_read_bit(bits)];
        if (bits->eof) return -1;
        if ((code < 0) || (code >= SLS_T + SLS_N_CHAR)) return -1;
    }
    code -= SLS_T;
    if ((code < 0) || (code >= SLS_N_CHAR)) return -1;
    if (!sls_update(state, code)) return -1;
    return code;
}

/* 13-bit position: the table supplies the top six bits, the low seven come
 * from the accumulated prefix byte.  This is the part that differs from the
 * classic 6-low-bit LZHUF position code. */
static int32_t sls_decode_position(sls_bits *bits, const sls_state *state) {
    int32_t byte = sls_read_byte(bits);
    int32_t high;
    int32_t extra;
    (void)state;
    if (bits->eof) return -1;
    high = (int32_t)xx_sls_d_code[byte & 0xff] << 7;
    extra = (int32_t)xx_sls_d_len[(byte >> 4) & 0x0f] - 1;
    while (extra > 0) {
        --extra;
        byte = ((byte << 1) + sls_read_bit(bits)) & 0xffff;
        if (bits->eof) return -1;
    }
    return high | (byte & 0x7f);
}

bool xx_sls_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    sls_state *state;
    sls_bits bits;
    size_t produced = 0U;
    uint32_t ring_position = 0U;
    const uint32_t mask = (uint32_t)SLS_N_MASK;
    bool ok = false;

    if (written) *written = 0U;
    if (output_size == 0U) return true;
    if (!input || !output || (input_size == 0U)) return false;

    state = (sls_state *)xx_mem_alloc(sizeof(sls_state));
    if (!state) return false;
    xx_rt_memset(state, 0, sizeof(*state));
    sls_init_tree(state);
    /* The ring is prefilled with spaces, so an early back-reference that points
     * "before" the output is legitimate and yields 0x20 -- the format's defined
     * initial window, not an out-of-range access. */
    xx_rt_memset(state->ring, 0x20, sizeof(state->ring));

    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.buffer = 0U;
    bits.count = 0;
    bits.eof = false;

    while (produced < output_size) {
        int32_t symbol = sls_decode_character(state, &bits);
        if ((symbol < 0) || bits.eof) goto done;
        if (symbol < 256) {
            output[produced++] = (uint8_t)symbol;
            state->ring[ring_position] = (uint8_t)symbol;
            ring_position = (ring_position + 1U) & mask;
            continue;
        }
        {
            int32_t position = sls_decode_position(&bits, state);
            size_t length = (size_t)(symbol + SLS_THRESHOLD - 0xff);
            uint32_t source;
            if ((position < 0) || bits.eof) goto done;
            /* The reference clamps a final overlong match to the remaining
             * plaintext instead of rejecting the member; the stream has no stop
             * code, so the last match legitimately overshoots.  Deliberate --
             * do not turn this into an error. */
            if (length > (output_size - produced)) {
                length = output_size - produced;
            }
            source = ((ring_position - (uint32_t)position) - 1U) & mask;
            while (length > 0U) {
                uint8_t value = state->ring[source & mask];
                output[produced++] = value;
                state->ring[ring_position] = value;
                ring_position = (ring_position + 1U) & mask;
                source = (source & mask) + 1U;
                --length;
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
