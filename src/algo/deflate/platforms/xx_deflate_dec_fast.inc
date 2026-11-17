/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Unchecked Huffman block loop, included once per match-copy backend by
 * xx_deflate_dec.c with XXDF_NAME (function name), XXDF_ATTR (target
 * attribute), XXDF_COPY(dst, distance, length), XXDF_SHL(v, n),
 * XXDF_SHR(v, e) and XXDF_EXTRA(e) (the extra bits of a length/distance
 * entry from saved) defined, and XXDF_SPEC for the speculative instance.
 *
 * Entry conditions keep every load and store inside its buffer:
 * - at least XX_DEC_FAST_IN bytes of input remain in the current span, so
 *   the at most three unaligned 8-byte refills of one iteration (each
 *   advancing in by <= 7 bytes, the last reading up to in + 22) stay inside;
 * - at least XX_DEC_FAST_OUT bytes of output room remain before limit, so
 *   three literals always fit; a match is copied with the
 *   overlapping backend only if length + XX_DEC_COPY_SLACK bytes fit,
 *   otherwise through the exact (checked) helper.
 * Every entry is consumed with one shift by its total bit count as soon as
 * it is loaded (see the table layout in xx_deflate_dec.c); the extra bits
 * of a length/distance come from the bits saved before that shift.
 * Bit budget per iteration: a refill leaves >= 56 valid bits; main-table
 * literal entries take <= 11 bits each, so three take <= 33, and two plus a
 * length code with extra bits <= 53 (22 + 11 + 4 + 16 for a Deflate64
 * sub-table length). The distance (<= 9 + 6 + 14 bits) is preceded by a
 * refill whenever fewer than 29 bits remain.
 * The symbols handled per iteration, the refill points and the room checks
 * define where the loop hands over to the checked decoder and where the
 * output is flushed; they are part of the observable behaviour (device
 * write sizes, partial output after an error) and must not change.
 * bitsleft is decremented by whole entries (libdeflate's trick): an entry's
 * low byte is exactly its total bit count (<= 31, see the table layout), so
 * only the low byte of bitsleft is meaningful and XXDF_BITS() recovers it;
 * the bits above it collect garbage from the entries' upper fields.
 * Bits above XXDF_BITS() in bitbuf are real look-ahead input (refills
 * re-OR the same bytes there); they are cleared before the state goes back
 * to the reader.
 * Returns 1 at the end-of-block code, 0 when the fast region is left (the
 * caller continues with the checked decoder), -1 on an invalid stream or
 * an output failure. */
#ifndef XXDF_HELPERS
#define XXDF_HELPERS
#define XXDF_BITS() (bitsleft & 0xFFU)
/* The shift counts are taken modulo 64 (bitsleft's low byte is < 64);
 * XXDF_SHL/XXDF_SHR may be BMI2 shifts, which do that in hardware. */
#define XXDF_REFILL()                                         \
    do {                                                      \
        bitbuf |= XXDF_SHL(xx_dec_load64(in), bitsleft);      \
        in += 7U - ((bitsleft >> 3) & 7U);                    \
        bitsleft |= 56U;                                      \
    } while (0)
#define XXDF_CONSUME(e)                            \
    do {                                           \
        saved = bitbuf;                            \
        bitbuf = XXDF_SHR(bitbuf, e);              \
        bitsleft -= (e);                           \
    } while (0)
#define XXDF_PUT_LIT(e) (*pos++ = (uint8_t)((e) >> 16))
#endif

/* With XXDF_SPEC defined (the instance for blocks whose main-table literals
 * of XX_DEC_SPEC_BITS bits carry XX_HF_SPEC), each iteration first looks up
 * the three literals and the next entry at the fixed offsets 0, S, 2S and
 * 3S (S = XX_DEC_SPEC_BITS) of the refilled bit buffer at once, and keeps
 * them only if the first three are flagged literals, i.e. literals of
 * exactly S bits: then they are the very entries (and the state the very
 * state) of the sequential lookups below. Otherwise the iteration runs the
 * sequential path. The table loads thus leave the loop-carried dependency
 * chain, which is otherwise one dependent load per literal. */
static XXDF_ATTR int XXDF_NAME(const uint32_t *lit_table, const uint32_t *dist_table, xx_bit_reader *br, xx_out_lin *out)
{
    const uint8_t *in, *in_end, *in_start;
    uint32_t entry;
    uint64_t bitbuf = br->bit_buf, saved;
    unsigned bitsleft = (unsigned)br->bit_count;
    uint8_t *pos = out->pos;
    uint8_t *limit = out->limit;
    uint8_t *base = out->base;
    const size_t win_size = out->win_size;
    int result = 0;

    if (br->dev) {
        in_start = br->buffer;
        in = br->buffer + br->buffer_pos;
        in_end = br->buffer + br->buffer_len;
    } else {
        in_start = br->mem_src;
        in = br->mem_src + br->mem_pos;
        in_end = br->mem_src + br->mem_size;
    }

    /* The next litlen entry is looked up as early as possible (before the
     * refill and before the match copy) so that its load overlaps other
     * work; a refill only ORs bits above bitsleft, so it stays valid. */
    if ((size_t)(in_end - in) >= XX_DEC_FAST_IN) XXDF_REFILL();
    entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
    while ((size_t)(in_end - in) >= XX_DEC_FAST_IN) {
        size_t length, distance;
        if ((size_t)(limit - pos) < XX_DEC_FAST_OUT) {
            out->pos = pos;
            if (!xx_out_make_room(out, XX_DEC_FAST_OUT)) {
                pos = out->pos;
                if (out->error) result = -1;
                break;
            }
            pos = out->pos;
            limit = out->limit;
            base = out->base;
        }
        XXDF_REFILL();

#ifdef XXDF_SPEC
        {
            /* 3 * S + 11 <= 35 bits of the >= 56 refilled ones */
            uint32_t e2 = lit_table[(bitbuf >> XX_DEC_SPEC_BITS) & XX_DEC_LIT_MASK];
            uint32_t e3 = lit_table[(bitbuf >> (2U * XX_DEC_SPEC_BITS)) & XX_DEC_LIT_MASK];
            uint32_t e4 = lit_table[(bitbuf >> (3U * XX_DEC_SPEC_BITS)) & XX_DEC_LIT_MASK];
            if (entry & e2 & e3 & XX_HF_SPEC) {
                pos[0] = (uint8_t)(entry >> 16);
                pos[1] = (uint8_t)(e2 >> 16);
                pos[2] = (uint8_t)(e3 >> 16);
                pos += 3;
                bitbuf >>= 3U * XX_DEC_SPEC_BITS;
                bitsleft -= 3U * XX_DEC_SPEC_BITS;
                entry = e4;
                continue;
            }
            /* Resuming after one or two right guesses was measured slower
             * (the extra branches mispredict), so a miss redoes all three. */
        }
#endif

        XXDF_CONSUME(entry);
        if (entry & XX_HF_LIT) {
            XXDF_PUT_LIT(entry);
            entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
            XXDF_CONSUME(entry);
            if (entry & XX_HF_LIT) {
                XXDF_PUT_LIT(entry);
                entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
                XXDF_CONSUME(entry);
                if (entry & XX_HF_LIT) {
                    XXDF_PUT_LIT(entry);
                    entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
                    continue;
                }
            }
        }
        if (entry & XX_HF_EXC) {
            if (entry & XX_HF_SUB) {
                entry = lit_table[(entry >> 16) + ((uint32_t)bitbuf & ((1U << XX_HE_CL(entry)) - 1U))];
                XXDF_CONSUME(entry);
                if (entry & XX_HF_LIT) {
                    *pos++ = (uint8_t)(entry >> 16);
                    entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
                    continue;
                }
            }
            if (entry & XX_HF_EXC) {
                if (entry & XX_HF_EOB) {
                    result = 1;
                } else {
                    br->error = true;
                    result = -1;
                }
                break;
            }
        }
        /* The distance entry is looked up before the conditional refill:
         * at most 53 bits were consumed since the last refill, so the low
         * 64 - 53 bits of bitbuf are real input either way. */
        length = (size_t)(entry >> 16) + XXDF_EXTRA(entry);
        entry = dist_table[bitbuf & XX_DEC_DIST_MASK];
        if (XXDF_BITS() < 29U) XXDF_REFILL();
        if (entry & XX_HF_EXC) {
            if (!(entry & XX_HF_SUB)) {
                br->error = true;
                result = -1;
                break;
            }
            bitbuf >>= XX_DEC_DIST_BITS;
            bitsleft -= XX_DEC_DIST_BITS;
            entry = dist_table[(entry >> 16) + ((uint32_t)bitbuf & ((1U << XX_HE_CL(entry)) - 1U))];
            if (entry & XX_HF_EXC) {
                br->error = true;
                result = -1;
                break;
            }
        }
        XXDF_CONSUME(entry);
        distance = (size_t)(entry >> 16) + XXDF_EXTRA(entry);
        /* Third refill of the iteration (see XX_DEC_FAST_IN), then the
         * preload of the next litlen entry ahead of the copy. */
        XXDF_REFILL();
        entry = lit_table[bitbuf & XX_DEC_LIT_MASK];
        if (distance > (size_t)(pos - base) || distance > win_size) {
            result = -1;
            break;
        }
        if ((size_t)(limit - pos) < length + XX_DEC_COPY_SLACK) {
            out->pos = pos;
            if (!xx_out_copy_checked(out, distance, length)) {
                pos = out->pos; /* keeps the stored overflow prefix */
                result = -1;
                break;
            }
            pos = out->pos;
            limit = out->limit;
            base = out->base;
            continue;
        }
        XXDF_COPY(pos, distance, length);
        pos += length;
    }

    bitsleft = XXDF_BITS();
    br->bit_buf = bitbuf & ((UINT64_C(1) << bitsleft) - 1U);
    br->bit_count = (int)bitsleft;
    if (br->dev) br->buffer_pos = (size_t)(in - in_start);
    else br->mem_pos = (size_t)(in - in_start);
    out->pos = pos;
    return result;
}
