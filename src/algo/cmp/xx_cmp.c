/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The two codecs of the ".CMP" container (header word 0x007F), ported from
 * XArchive's XCMPDecoder plus the parameterisation of XSharedLZWDecoder that
 * it selects (nMaxBits 16, clear code, end code, MSB-first, no block padding,
 * width-step bias 1).
 */
#include "xxfclib/algo/cmp/xx_cmp.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* ---------------------------------------------------------------------- */
/* Method 1: framed LZW                                                    */
/* ---------------------------------------------------------------------- */

#define CMP_LZW_CLEAR 0x100U
#define CMP_LZW_END 0x101U
#define CMP_LZW_FIRST_FREE 0x102U /* clear code and end code both present */
#define CMP_LZW_MAX_BITS 16U
#define CMP_LZW_CAPACITY 0x10000U /* 1 << CMP_LZW_MAX_BITS */
#define CMP_LZW_STACK_SIZE (CMP_LZW_CAPACITY + 2U)

/* The method 1 framing works in blocks of this size - the same value the
 * container validates in the word at header offset 0x37.  A frame that
 * reaches it is a block the compressor could not shrink, so it was stored
 * raw.  DELIBERATE DIVERGENCE FROM THE ORIGINAL PROGRAM, kept from the
 * XArchive reference: the original feeds such a frame to the LZW decoder,
 * which turns it into a few bytes of noise and loses the rest of the member.
 * Do not "fix" this back. */
#define CMP_BLOCK_SIZE 0x1000U

/* Bias on the free-slot count that triggers the width step.  Zero is the
 * usual "step when the next free slot no longer fits"; one steps a single
 * code earlier, which is what the CMP framing uses.  Getting this wrong
 * desyncs only after the first 512-code boundary, so short members still
 * decode and the bug hides.  DELIBERATE - do not change to 0. */
#define CMP_LZW_WIDTH_BIAS 1U

typedef struct cmp_lzw_ctx_s {
    uint32_t *prefix; /* CMP_LZW_CAPACITY entries                           */
    uint8_t *suffix;  /* CMP_LZW_CAPACITY entries                           */
    uint8_t *stack;   /* CMP_LZW_STACK_SIZE entries                         */
    uint32_t high_water; /* highest slot ever written since the last clear  */
} cmp_lzw_ctx;

typedef struct cmp_lzw_reader_s {
    const uint8_t *data;
    uint64_t bits;    /* total bit count of the frame                       */
    uint64_t bit_pos;
    uint32_t width;
    uint32_t max_code;
    uint32_t free_slot;
} cmp_lzw_reader;

static void cmp_lzw_reader_reset(cmp_lzw_reader *reader) {
    reader->width = 9U;
    reader->max_code = 0x1ffU;
    reader->free_slot = CMP_LZW_FIRST_FREE;
}

static bool cmp_lzw_next(cmp_lzw_reader *reader, uint32_t *code) {
    uint32_t value = 0U;
    uint32_t index;

    /* The width step is evaluated BEFORE the read, and a new entry is added
     * AFTER the phrase is emitted.  Both orderings are load bearing. */
    if (reader->max_code < (reader->free_slot + CMP_LZW_WIDTH_BIAS)) {
        ++reader->width;
        reader->max_code = (reader->width == CMP_LZW_MAX_BITS)
                               ? CMP_LZW_CAPACITY
                               : ((1U << reader->width) - 1U);
    }

    /* A code that runs past the final byte is still readable - the encoder
     * pads the tail with zero bits - but a code that STARTS past it is not. */
    if (reader->bit_pos >= reader->bits) return false;

    for (index = 0U; index < reader->width; ++index) {
        uint64_t bit = reader->bit_pos + index;
        uint32_t set = 0U;
        if (bit < reader->bits)
            set = (uint32_t)((reader->data[(size_t)(bit >> 3)] >>
                              (7U - (unsigned)(bit & 7U))) &
                             1U);
        value = (value << 1U) | set;
    }
    reader->bit_pos += reader->width;
    *code = value;
    return true;
}

/*
 * Decode one LZW frame.  Returns false only for the reference's two hard
 * failures, which discard the frame's output entirely and let the caller move
 * on to the next frame (XSharedLZWDecoder::decode returns before assigning
 * its result, leaving the caller with an empty chunk).  Every other stop -
 * end code, exhausted bits - is a normal break that keeps what was produced.
 *
 * @p limit is the frame's own ceiling - the full declared plaintext size, as
 * the reference passes it, because how far the frame runs decides whether it
 * reaches a hard failure at all.  @p room is how much of that the caller can
 * still keep; bytes past it are counted but not stored, which is exactly the
 * reference's "append the whole chunk, truncate the member at the end".
 */
static bool cmp_lzw_decode_frame(cmp_lzw_ctx *ctx, const uint8_t *input,
                                 size_t input_size, uint8_t *output,
                                 size_t room, size_t limit, size_t *written) {
    cmp_lzw_reader reader;
    size_t out_pos = 0U;
    size_t stack_size;
    size_t index;
    uint32_t code = 0U;
    uint32_t current;
    uint32_t guard;
    uint32_t previous = 0U;
    uint8_t previous_first = 0U;
    bool has_previous = false;

    *written = 0U;

    /* Slots written by earlier frames must read back as zero, exactly as the
     * reference's per-frame zeroed prefix vector does: a malformed stream can
     * store a forward code and later walk into a slot it never wrote. */
    if (ctx->high_water > CMP_LZW_FIRST_FREE) {
        size_t stale = (size_t)(ctx->high_water - CMP_LZW_FIRST_FREE);
        xx_rt_memset(ctx->prefix + CMP_LZW_FIRST_FREE, 0,
                     stale * sizeof(uint32_t));
        xx_rt_memset(ctx->suffix + CMP_LZW_FIRST_FREE, 0, stale);
    }
    ctx->high_water = CMP_LZW_FIRST_FREE;

    if (input_size > (SIZE_MAX / 8U)) return false;
    reader.data = input;
    reader.bits = (uint64_t)input_size * 8U;
    reader.bit_pos = 0U;
    cmp_lzw_reader_reset(&reader);

    while (out_pos < limit) {
        if (!cmp_lzw_next(&reader, &code)) break;
        if (code == CMP_LZW_END) break;

        if (code == CMP_LZW_CLEAR) {
            cmp_lzw_reader_reset(&reader);
            if (!cmp_lzw_next(&reader, &code)) break;
            /* A non-literal straight after a clear is a normal break here,
             * NOT the hard failure the has_previous branch below uses.  The
             * reference distinguishes the two; keep them distinct. */
            if (code > 0xffU) break;
            previous = code;
            previous_first = (uint8_t)code;
            has_previous = true;
            if (out_pos < room) output[out_pos] = (uint8_t)code;
            ++out_pos;
            continue;
        }

        if (!has_previous) {
            if (code > 0xffU) return false; /* hard failure: frame discarded */
            previous = code;
            previous_first = (uint8_t)code;
            has_previous = true;
            if (out_pos < room) output[out_pos] = (uint8_t)code;
            ++out_pos;
            continue;
        }

        stack_size = 0U;
        current = code;
        if (code >= reader.free_slot) {
            /* KwKwK appends the PREVIOUS PHRASE's first byte, not the low
             * byte of the previous code.  The two agree whenever the previous
             * code was a literal, which is why the usual mistake survives
             * most small members. */
            ctx->stack[stack_size++] = previous_first;
            current = previous;
        }
        guard = 0U;
        while (current > 0xffU) {
            if ((current >= CMP_LZW_CAPACITY) || (++guard > CMP_LZW_CAPACITY))
                return false; /* hard failure: frame discarded */
            ctx->stack[stack_size++] = ctx->suffix[current];
            current = ctx->prefix[current];
        }
        ctx->stack[stack_size++] = (uint8_t)current;

        for (index = stack_size; index > 0U; --index) {
            if (out_pos < room) output[out_pos] = ctx->stack[index - 1U];
            ++out_pos;
        }

        if (reader.free_slot < CMP_LZW_CAPACITY) {
            ctx->prefix[reader.free_slot] = previous;
            ctx->suffix[reader.free_slot] = (uint8_t)current;
            if (reader.free_slot >= ctx->high_water)
                ctx->high_water = reader.free_slot + 1U;
            ++reader.free_slot;
        }
        previous = code;
        previous_first = (uint8_t)current;
    }

    *written = out_pos;
    return true;
}

bool xx_cmp_lzw_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    cmp_lzw_ctx ctx;
    size_t offset = 0U;
    size_t out_pos = 0U;
    size_t frame;
    size_t remaining;
    size_t produced;
    uint32_t index;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;
    /* A zero-length member decodes to zero bytes, as in the reference. */
    if (output_size == 0U) return true;

    ctx.prefix = (uint32_t *)xx_mem_calloc(CMP_LZW_CAPACITY, sizeof(uint32_t));
    ctx.suffix = (uint8_t *)xx_mem_calloc(CMP_LZW_CAPACITY, sizeof(uint8_t));
    ctx.stack = (uint8_t *)xx_mem_calloc(CMP_LZW_STACK_SIZE, sizeof(uint8_t));
    ctx.high_water = CMP_LZW_FIRST_FREE;
    if (!ctx.prefix || !ctx.suffix || !ctx.stack) {
        xx_mem_free(ctx.prefix);
        xx_mem_free(ctx.suffix);
        xx_mem_free(ctx.stack);
        return false;
    }
    for (index = 0U; index < 256U; ++index) ctx.suffix[index] = (uint8_t)index;

    while ((offset + 2U) <= input_size) {
        frame = (size_t)input[offset] | ((size_t)input[offset + 1U] << 8U);
        offset += 2U;
        if ((frame == 0U) || (frame > (input_size - offset))) break;

        remaining = output_size - out_pos;
        if (frame >= CMP_BLOCK_SIZE) {
            size_t count = (frame < remaining) ? frame : remaining;
            xx_rt_memcpy(output + out_pos, input + offset, count);
            out_pos += count;
        } else {
            /* Each frame is a complete stream that ends on its own end code,
             * so the per-frame room is only a ceiling, never the stopping
             * rule.  An empty frame is not the end of the member either: the
             * frame count is what ends the loop, and offset always moves
             * forward. */
            produced = 0U;
            if (!cmp_lzw_decode_frame(&ctx, input + offset, frame,
                                      output + out_pos, remaining, output_size,
                                      &produced))
                produced = 0U;
            out_pos += (produced < remaining) ? produced : remaining;
        }

        offset += frame;
        if (out_pos >= output_size) break;
    }

    xx_mem_free(ctx.prefix);
    xx_mem_free(ctx.suffix);
    xx_mem_free(ctx.stack);

    if (written) *written = out_pos;
    return out_pos == output_size;
}

/* ---------------------------------------------------------------------- */
/* Method 2: 9-bit LZSS                                                    */
/* ---------------------------------------------------------------------- */

#define CMP_LZSS_WINDOW 0x800U
#define CMP_LZSS_MASK (CMP_LZSS_WINDOW - 1U)

/*
 * MSB-first reader over a 32-bit accumulator, matching the reference bit
 * reader exactly: it keeps feeding ONE zero byte once the input is exhausted
 * and only fails a byte later, which is what lets a truncated stream produce
 * the same prefix the original implementation writes.  DELIBERATE.
 */
typedef struct cmp_lzss_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    uint32_t count;
} cmp_lzss_bits;

static bool cmp_lzss_read(cmp_lzss_bits *reader, uint32_t nbits,
                          uint32_t *value) {
    uint32_t byte;

    if (nbits == 0U) {
        *value = 0U;
        return true;
    }
    /* nbits is at most 9 here, so count is at most 8 when the shift below
     * runs and 24 - count never goes negative. */
    while (reader->count < nbits) {
        byte = 0U;
        if (reader->position > reader->size) return false;
        if (reader->position < reader->size)
            byte = reader->data[reader->position];
        ++reader->position;
        reader->accumulator += (uint32_t)(byte << (24U - reader->count));
        reader->count += 8U;
    }
    *value = reader->accumulator >> (32U - nbits);
    reader->accumulator = (uint32_t)(reader->accumulator << nbits);
    reader->count -= nbits;
    return true;
}

/*
 * The length is a two-bit value; a value of 3 takes two more bits, and if
 * those are also 3 it keeps adding nibbles while each nibble is 15.  The
 * caller adds two, so the shortest match is two bytes.
 *
 * Accumulated in 64 bits rather than the reference's int, which can only
 * differ for a stream long enough to overflow 32 bits; such a length always
 * exceeds the output room and fails in the caller either way.
 */
static bool cmp_lzss_read_length(cmp_lzss_bits *reader, uint64_t *length) {
    uint32_t value = 0U;
    uint32_t more = 0U;
    uint32_t nibble = 0U;
    uint64_t result;

    if (!cmp_lzss_read(reader, 2U, &value)) return false;
    result = value;
    if (result == 3U) {
        if (!cmp_lzss_read(reader, 2U, &more)) return false;
        result += more;
        if (more == 3U) {
            do {
                if (!cmp_lzss_read(reader, 4U, &nibble)) return false;
                result += nibble;
            } while (nibble == 15U);
        }
    }
    *length = result;
    return true;
}

bool xx_cmp_lzss_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written) {
    uint8_t window[CMP_LZSS_WINDOW];
    cmp_lzss_bits reader;
    size_t out_pos = 0U;
    uint64_t counter;
    uint64_t length;
    uint32_t position = 0U;
    uint32_t token = 0U;
    uint32_t low;
    uint32_t extra = 0U;
    uint32_t distance;
    uint8_t byte;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;
    if (output_size == 0U) return true;

    xx_rt_memset(window, 0, sizeof(window));
    reader.data = input;
    reader.size = input_size;
    reader.position = 0U;
    reader.accumulator = 0U;
    reader.count = 0U;

    while (out_pos < output_size) {
        if (!cmp_lzss_read(&reader, 9U, &token)) break;

        if (token < 0x100U) {
            window[position] = (uint8_t)token;
            output[out_pos++] = (uint8_t)token;
            position = (position + 1U) & CMP_LZSS_MASK;
            continue;
        }

        low = token & 0xffU;

        if (low == 0x81U) {
            /* Repeat the byte just written, which is a distance-1 match: the
             * reference hoists that byte out of the loop, but re-reading it
             * through the ring every step yields the same value, because the
             * step before it wrote exactly that byte there.  Sharing the copy
             * below also keeps the compiler from recognising a constant fill
             * and calling into the CRT's memset. */
            distance = 1U;
        } else if (low < 0x80U) {
            if (!cmp_lzss_read(&reader, 4U, &extra)) break;
            distance = (low * 16U) + extra; /* at most 0x7f*16+15 == 2047 */
        } else {
            distance = low & 0x7fU;
            /* low == 0x80 is the end-of-stream token and lands here as a
             * zero distance.  DELIBERATE - it is the only way the stream
             * says it is finished. */
            if (distance == 0U) break;
        }

        if (!cmp_lzss_read_length(&reader, &length)) break;
        length += 2U;
        /* Overshooting the declared size is a failure, not a truncation: the
         * reference produces more than the declared size and then reports
         * failure, so breaking here is equivalent. */
        if (length > (uint64_t)(output_size - out_pos)) break;
        for (counter = 0U; counter < length; ++counter) {
            /* The ring is the bound: the index is masked into 0..2047, so a
             * reference reaching back before the start of output reads the
             * zero-filled window instead of failing.  That is what the
             * reference does and some real members rely on it. */
            byte = window[(position - distance) & CMP_LZSS_MASK];
            window[position] = byte;
            output[out_pos++] = byte;
            position = (position + 1U) & CMP_LZSS_MASK;
        }
    }

    if (written) *written = out_pos;
    return out_pos == output_size;
}
