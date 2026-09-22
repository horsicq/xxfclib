/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KolibriOS kpack ("KPCK") container decoder.  Ported one-for-one from the
 * XArchive reference decoder (XArchive/Algos/xkolibrikpackdecoder.cpp).
 *
 * DELIBERATE, DO NOT "FIX":
 *
 *  - THE RANGE CODER IS INITIALISED FROM FOUR BYTES, LITTLE-ENDIAN.  Stock
 *    LZMA skips one byte and reads four big-endian; kpack's own unpacker
 *    byte-swaps the first dword in place before calling a stock five-byte
 *    initialiser, which nets out to "code = the first four payload bytes read
 *    little-endian, no byte skipped".  Range starts at 0xFFFFFFFF as usual.
 *
 *  - THERE IS NO LZMA PROPERTIES BYTE.  lc=3 lp=0 pb=2 are hard-wired.
 *
 *  - READING PAST THE END OF THE STREAM IS TOLERATED.  The reference feeds
 *    zeroes once the input is exhausted and records the fact in a flag it then
 *    never tests, so a stream that normalises a few bytes past its end still
 *    decodes.  Kept, because the only acceptance test that matters is "the
 *    full declared plaintext was produced"; a stream that is genuinely short
 *    stops producing bytes and the final length comparison rejects it.
 *
 *  - THE LAST MATCH IS CLAMPED, NOT REJECTED.  A match that would run past the
 *    declared plaintext length is copied only as far as the buffer goes and
 *    the decode then ends successfully.
 *
 *  - THE CALL-TRICK TRAILER IS AT THE END OF THE CONTAINER, and the filtered
 *    32-bit targets are stored with only their top three bytes behind a marker
 *    byte (marker, hi, mid, lo), big-endian.  The conversion count comes from
 *    the trailer, and the pass stops after exactly that many conversions; a
 *    buffer that ends before the count is reached is a failure.
 *
 * DEVIATION: none.  The state lives in one caller-owned heap block instead of
 * C++ std::vector members; the probability array layout, the indexing and the
 * order of every range-coder operation are unchanged.
 */
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/kolibrikpack/xx_kolibrikpack.h"

#define KPACK_NUM_BIT_MODEL_TOTAL_BITS 11
#define KPACK_NUM_MOVE_BITS 5
#define KPACK_PROB_INIT ((uint16_t)((1U << KPACK_NUM_BIT_MODEL_TOTAL_BITS) / 2))
#define KPACK_TOP_VALUE (1U << 24)

#define KPACK_LC 3
#define KPACK_PB_MASK 3 /* pb == 2 */

#define KPACK_FLAG_LZMA 0x01U
#define KPACK_FLAG_CALLTRICK1 0x40U
#define KPACK_FLAG_CALLTRICK2 0x80U
#define KPACK_CALLTRICK_TRAILER_SIZE 5

#define KPACK_HEADER_SIZE 12
#define KPACK_MAX_INPUT_SIZE 0x10000000  /* 256 MiB */
#define KPACK_MAX_OUTPUT_SIZE 0x20000000 /* 512 MiB */

#define KPACK_SPEC_POS_SIZE 128
#define KPACK_LITERAL_SIZE (0x300 << KPACK_LC)

typedef struct kpack_rc_s {
    const uint8_t *data;
    uint64_t size;
    uint64_t position; /* keeps advancing past the end so overrun is visible */
    uint32_t range;
    uint32_t code;
} kpack_rc;

typedef struct kpack_probs_s {
    uint16_t is_match[192];
    uint16_t is_rep[12];
    uint16_t is_rep_g0[12];
    uint16_t is_rep_g1[12];
    uint16_t is_rep_g2[12];
    uint16_t is_rep0_long[192];
    uint16_t pos_slot[256];
    uint16_t spec_pos[KPACK_SPEC_POS_SIZE];
    uint16_t align[16];
    uint16_t len_choice[2];
    uint16_t len_low[128];
    uint16_t len_mid[128];
    uint16_t len_high[256];
    uint16_t rep_len_choice[2];
    uint16_t rep_len_low[128];
    uint16_t rep_len_mid[128];
    uint16_t rep_len_high[256];
    uint16_t literal[KPACK_LITERAL_SIZE];
} kpack_probs;

static uint32_t kpack_next_byte(kpack_rc *rc) {
    if (rc->position < rc->size) {
        const uint32_t result = rc->data[rc->position];
        ++rc->position;
        return result;
    }

    /* A well formed LZMA stream may be normalised a few bytes past its own
     * end; keep feeding zeroes.  The reference records this and never tests
     * it, so neither does this port -- see the header comment. */
    ++rc->position;

    return 0U;
}

static void kpack_normalize(kpack_rc *rc) {
    while (rc->range < KPACK_TOP_VALUE) {
        rc->range <<= 8;
        rc->code = (rc->code << 8) | kpack_next_byte(rc);
    }
}

static void kpack_rc_init(kpack_rc *rc, const uint8_t *data, uint64_t size, uint64_t position) {
    uint32_t b0;
    uint32_t b1;
    uint32_t b2;
    uint32_t b3;

    rc->data = data;
    rc->size = size;
    rc->position = position;
    rc->range = 0xFFFFFFFFU;
    rc->code = 0U;

    /* Exactly four bytes, little-endian -- deliberate, see header comment. */
    b0 = kpack_next_byte(rc);
    b1 = kpack_next_byte(rc);
    b2 = kpack_next_byte(rc);
    b3 = kpack_next_byte(rc);

    rc->code = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static uint32_t kpack_decode_bit(kpack_rc *rc, uint16_t *prob) {
    const uint32_t bound = (rc->range >> KPACK_NUM_BIT_MODEL_TOTAL_BITS) * (uint32_t)(*prob);
    uint32_t result = 0U;

    if (rc->code < bound) {
        rc->range = bound;
        *prob = (uint16_t)((uint32_t)(*prob) + (((1U << KPACK_NUM_BIT_MODEL_TOTAL_BITS) - (uint32_t)(*prob)) >> KPACK_NUM_MOVE_BITS));
    } else {
        rc->range -= bound;
        rc->code -= bound;
        *prob = (uint16_t)((uint32_t)(*prob) - ((uint32_t)(*prob) >> KPACK_NUM_MOVE_BITS));
        result = 1U;
    }

    kpack_normalize(rc);

    return result;
}

static uint32_t kpack_decode_direct_bits(kpack_rc *rc, int32_t count) {
    uint32_t result = 0U;
    int32_t i;

    for (i = 0; i < count; ++i) {
        uint32_t t;

        rc->range >>= 1;
        rc->code -= rc->range;
        t = 0U - (rc->code >> 31);
        rc->code += rc->range & t;
        result = (result << 1) + t + 1U;
        kpack_normalize(rc);
    }

    return result;
}

static uint32_t kpack_bit_tree_decode(kpack_rc *rc, uint16_t *probs, int32_t bits) {
    uint32_t model = 1U;
    int32_t i;

    for (i = 0; i < bits; ++i) {
        model = (model << 1) | kpack_decode_bit(rc, probs + model);
    }

    return model - (1U << bits);
}

static uint32_t kpack_bit_tree_reverse_decode(kpack_rc *rc, uint16_t *probs, int32_t bits) {
    uint32_t model = 1U;
    uint32_t symbol = 0U;
    int32_t i;

    for (i = 0; i < bits; ++i) {
        const uint32_t bit = kpack_decode_bit(rc, probs + model);
        model = (model << 1) | bit;
        symbol |= bit << i;
    }

    return symbol;
}

static uint32_t kpack_decode_length(kpack_rc *rc, uint16_t *choice, uint16_t *low, uint16_t *mid, uint16_t *high, uint32_t pos_state) {
    if (kpack_decode_bit(rc, &choice[0]) == 0U) {
        return kpack_bit_tree_decode(rc, low + (pos_state << 3), 3) + 2U;
    }

    if (kpack_decode_bit(rc, &choice[1]) == 0U) {
        return kpack_bit_tree_decode(rc, mid + (pos_state << 3), 3) + 10U;
    }

    return kpack_bit_tree_decode(rc, high, 8) + 18U;
}

static void kpack_probs_init(kpack_probs *probs) {
    size_t i;
    uint16_t *raw = (uint16_t *)probs;
    const size_t count = sizeof(kpack_probs) / sizeof(uint16_t);

    for (i = 0; i < count; ++i) raw[i] = KPACK_PROB_INIT;
}

/* Raw LZMA1 body with kpack's fixed lc=3 lp=0 pb=2 model. */
static bool kpack_decode_lzma(const uint8_t *input, uint64_t input_size, uint64_t stream_offset, uint8_t *output, uint64_t output_size) {
    kpack_rc rc;
    kpack_probs *probs;
    uint64_t out_pos = 0U;
    uint32_t state = 0U;
    uint32_t rep0 = 0U;
    uint32_t rep1 = 0U;
    uint32_t rep2 = 0U;
    uint32_t rep3 = 0U;
    bool ok = true;

    if ((!input) || (!output) || (output_size == 0U) || (stream_offset + 4U > input_size)) return false;

    probs = (kpack_probs *)xx_mem_alloc(sizeof(kpack_probs));
    if (!probs) return false;

    kpack_probs_init(probs);
    kpack_rc_init(&rc, input, input_size, stream_offset);

    while (out_pos < output_size) {
        const uint32_t pos_state = (uint32_t)(out_pos & (uint64_t)KPACK_PB_MASK);
        uint32_t length = 0U;
        uint32_t i;

        if (kpack_decode_bit(&rc, &probs->is_match[(state << 4) + pos_state]) == 0U) {
            const uint32_t prev_byte = (out_pos > 0U) ? output[out_pos - 1U] : 0U;
            uint16_t *lit = probs->literal + ((size_t)(prev_byte >> (8 - KPACK_LC)) * 0x300U);
            uint32_t symbol = 1U;

            if (state < 7U) {
                while (symbol < 0x100U) {
                    symbol = (symbol << 1) | kpack_decode_bit(&rc, lit + symbol);
                }
            } else {
                uint32_t match_byte;

                if ((uint64_t)rep0 + 1U > out_pos) {
                    ok = false;
                    break;
                }

                match_byte = output[out_pos - (uint64_t)rep0 - 1U];

                while (symbol < 0x100U) {
                    const uint32_t match_bit = (match_byte >> 7) & 1U;
                    uint32_t bit;

                    match_byte = (match_byte << 1) & 0xFFU;
                    bit = kpack_decode_bit(&rc, lit + (((1U + match_bit) << 8) + symbol));
                    symbol = (symbol << 1) | bit;

                    if (match_bit != bit) {
                        while (symbol < 0x100U) {
                            symbol = (symbol << 1) | kpack_decode_bit(&rc, lit + symbol);
                        }
                        break;
                    }
                }
            }

            output[out_pos] = (uint8_t)(symbol & 0xFFU);
            ++out_pos;
            state = (state < 4U) ? 0U : ((state < 10U) ? (state - 3U) : (state - 6U));
            continue;
        }

        if (kpack_decode_bit(&rc, &probs->is_rep[state]) != 0U) {
            if (out_pos == 0U) {
                ok = false;
                break;
            }

            if (kpack_decode_bit(&rc, &probs->is_rep_g0[state]) == 0U) {
                if (kpack_decode_bit(&rc, &probs->is_rep0_long[(state << 4) + pos_state]) == 0U) {
                    state = (state < 7U) ? 9U : 11U;
                    if ((uint64_t)rep0 + 1U > out_pos) {
                        ok = false;
                        break;
                    }
                    output[out_pos] = output[out_pos - (uint64_t)rep0 - 1U];
                    ++out_pos;
                    continue;
                }
            } else {
                uint32_t dist;

                if (kpack_decode_bit(&rc, &probs->is_rep_g1[state]) == 0U) {
                    dist = rep1;
                } else {
                    if (kpack_decode_bit(&rc, &probs->is_rep_g2[state]) == 0U) {
                        dist = rep2;
                    } else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }

                rep1 = rep0;
                rep0 = dist;
            }

            length = kpack_decode_length(&rc, probs->rep_len_choice, probs->rep_len_low, probs->rep_len_mid, probs->rep_len_high, pos_state);
            state = (state < 7U) ? 8U : 11U;
        } else {
            uint32_t len_to_pos_state;
            uint32_t pos_slot;

            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;

            length = kpack_decode_length(&rc, probs->len_choice, probs->len_low, probs->len_mid, probs->len_high, pos_state);
            state = (state < 7U) ? 7U : 10U;

            len_to_pos_state = ((length - 2U) < 3U) ? (length - 2U) : 3U;
            pos_slot = kpack_bit_tree_decode(&rc, probs->pos_slot + (len_to_pos_state << 6), 6);

            if (pos_slot < 4U) {
                rep0 = pos_slot;
            } else {
                const int32_t num_direct = (int32_t)((pos_slot >> 1) - 1U);

                rep0 = (2U | (pos_slot & 1U)) << num_direct;

                if (pos_slot < 14U) {
                    const int64_t base = (int64_t)rep0 - (int64_t)pos_slot;

                    if ((base < 0) || (base + ((int64_t)1 << num_direct) > (int64_t)KPACK_SPEC_POS_SIZE)) {
                        ok = false;
                        break;
                    }

                    rep0 += kpack_bit_tree_reverse_decode(&rc, probs->spec_pos + (size_t)base, num_direct);
                } else {
                    rep0 += kpack_decode_direct_bits(&rc, num_direct - 4) << 4;
                    rep0 += kpack_bit_tree_reverse_decode(&rc, probs->align, 4);
                }
            }

            if (rep0 == 0xFFFFFFFFU) {
                /* End marker. */
                break;
            }
        }

        if ((uint64_t)rep0 + 1U > out_pos) {
            ok = false;
            break;
        }

        for (i = 0U; i < length; ++i) {
            if (out_pos >= output_size) break;
            output[out_pos] = output[out_pos - (uint64_t)rep0 - 1U];
            ++out_pos;
        }
    }

    xx_mem_free(probs);

    return ok && (out_pos == output_size);
}

/* Undo kpack's x86 "call trick": absolute 32-bit targets stored big-endian
 * behind E8/E9 (and, for calltrick2, 0F 80..8F) become relative displacements
 * again.  Count and marker byte come from the container's 5-byte trailer. */
static bool kpack_undo_call_trick(uint8_t *data, uint64_t size, uint32_t count, uint8_t marker, bool call_trick2) {
    uint64_t pos = 0U;

    while (count) {
        uint32_t byte;
        bool accepted = false;
        uint32_t value;
        uint32_t relative;

        if (pos >= size) return false;

        byte = data[pos++];

        if (call_trick2 && (byte == 0x0FU)) {
            for (;;) {
                uint32_t next;

                if (pos >= size) return false;
                next = data[pos++];

                if (next < 0x80U) {
                    if (next == 0x0FU) continue;
                    byte = next;
                    break;
                }

                if (next < 0x90U) accepted = true;
                byte = next;
                break;
            }
        }

        if (!accepted) {
            if ((byte != 0xE8U) && (byte != 0xE9U)) continue;
        }

        if (pos >= size) return false;
        if (data[pos] != marker) continue;
        if (pos + 4U > size) return false;

        value = ((uint32_t)data[pos + 1U] << 16) | ((uint32_t)data[pos + 2U] << 8) | (uint32_t)data[pos + 3U];
        pos += 4U;

        relative = value - (uint32_t)pos;

        data[pos - 4U] = (uint8_t)(relative & 0xFFU);
        data[pos - 3U] = (uint8_t)((relative >> 8) & 0xFFU);
        data[pos - 2U] = (uint8_t)((relative >> 16) & 0xFFU);
        data[pos - 1U] = (uint8_t)((relative >> 24) & 0xFFU);

        --count;
    }

    return true;
}

static bool kpack_check_header(const uint8_t *data, uint64_t file_size, uint32_t *unpacked_size, uint32_t *flags) {
    uint32_t size_value;
    uint32_t flag_value;
    uint64_t minimum_size;

    if (!data) return false;

    if ((data[0] != 'K') || (data[1] != 'P') || (data[2] != 'C') || (data[3] != 'K')) return false;

    size_value = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    flag_value = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);

    /* kpack's own unpacker rejects everything but "LZMA plus at most one of the
     * two call-trick filters"; the upper 24 bits of the method dword are
     * unused. */
    if ((flag_value & ~(KPACK_FLAG_CALLTRICK1 | KPACK_FLAG_CALLTRICK2)) != KPACK_FLAG_LZMA) return false;
    if ((flag_value & (KPACK_FLAG_CALLTRICK1 | KPACK_FLAG_CALLTRICK2)) == (KPACK_FLAG_CALLTRICK1 | KPACK_FLAG_CALLTRICK2)) return false;

    if ((size_value == 0U) || ((uint64_t)size_value > (uint64_t)KPACK_MAX_OUTPUT_SIZE)) return false;

    minimum_size = (uint64_t)KPACK_HEADER_SIZE + 4U;
    if (flag_value & (KPACK_FLAG_CALLTRICK1 | KPACK_FLAG_CALLTRICK2)) {
        minimum_size += (uint64_t)KPACK_CALLTRICK_TRAILER_SIZE;
    }

    if (file_size < minimum_size) return false;

    if (unpacked_size) *unpacked_size = size_value;
    if (flags) *flags = flag_value;

    return true;
}

bool xx_kolibrikpack_check_header(const uint8_t *input, size_t input_size, size_t *produced) {
    uint32_t unpacked_size = 0U;

    if (produced) *produced = 0U;
    if (!input || (input_size < (size_t)KPACK_HEADER_SIZE)) return false;
    if (!kpack_check_header(input, (uint64_t)input_size, &unpacked_size, NULL)) return false;
    if (produced) *produced = (size_t)unpacked_size;

    return true;
}

bool xx_kolibrikpack_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written) {
    uint32_t unpacked_size = 0U;
    uint32_t flags = 0U;
    uint64_t stream_size;
    bool call_trick1;
    bool call_trick2;

    if (written) *written = 0U;
    if ((!input) || (!output && (output_size != 0U))) return false;

    if ((input_size < (size_t)(KPACK_HEADER_SIZE + 4)) || ((uint64_t)input_size > (uint64_t)KPACK_MAX_INPUT_SIZE)) return false;

    if (!kpack_check_header(input, (uint64_t)input_size, &unpacked_size, &flags)) return false;

    /* Running out of output capacity is a failure, never a truncation. */
    if ((uint64_t)unpacked_size > (uint64_t)output_size) return false;

    stream_size = (uint64_t)input_size;
    call_trick1 = (flags & KPACK_FLAG_CALLTRICK1) != 0U;
    call_trick2 = (flags & KPACK_FLAG_CALLTRICK2) != 0U;

    if (call_trick1 || call_trick2) {
        stream_size -= (uint64_t)KPACK_CALLTRICK_TRAILER_SIZE;
    }

    if (stream_size < (uint64_t)(KPACK_HEADER_SIZE + 4)) return false;

    if (!kpack_decode_lzma(input, stream_size, (uint64_t)KPACK_HEADER_SIZE, output, (uint64_t)unpacked_size)) return false;

    if (call_trick1 || call_trick2) {
        const uint8_t *trailer = input + (input_size - (size_t)KPACK_CALLTRICK_TRAILER_SIZE);
        const uint32_t count = (uint32_t)trailer[0] | ((uint32_t)trailer[1] << 8) | ((uint32_t)trailer[2] << 16) | ((uint32_t)trailer[3] << 24);
        const uint8_t marker = trailer[4];

        if ((uint64_t)count > (uint64_t)unpacked_size) return false;

        if (!kpack_undo_call_trick(output, (uint64_t)unpacked_size, count, marker, call_trick2)) return false;
    }

    if (written) *written = (size_t)unpacked_size;

    return true;
}
