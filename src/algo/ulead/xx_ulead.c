/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ULEAD ("U_LEAD CORP.") container codec, ported from XArchive's
 * XULEADDecoder (Algos/xuleaddecoder.cpp) together with the exact
 * XSharedLZWDecoder parameterisation it selects: nMaxBits 12, clear code,
 * end code, MSB-first, NO block padding, width-step bias 1.
 *
 * Because bBlockPadding is false for ULEAD, the shared decoder's
 * "padding counted in codes, not bits" path is never reached and is not
 * ported here; nothing else in that decoder is left out.
 */
#include "xxfclib/algo/ulead/xx_ulead.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define ULEAD_MAGIC_SIZE 12
#define ULEAD_HEADER1_SIZE 0x1c
#define ULEAD_HEADER2_SIZE 0x2c
#define ULEAD_NAME_OFFSET 0x18
#define ULEAD_NAME_SIZE 0x10

#define ULEAD_LZW_CLEAR 0x100U
#define ULEAD_LZW_END 0x101U
#define ULEAD_LZW_FIRST_FREE 0x102U /* clear code and end code both present */
#define ULEAD_LZW_MAX_BITS 12U
#define ULEAD_LZW_CAPACITY 0x1000U /* 1 << ULEAD_LZW_MAX_BITS */
#define ULEAD_LZW_STACK_SIZE (ULEAD_LZW_CAPACITY + 2U)
/* Bias on the free-slot count that triggers the width step.  Zero is the
 * usual "step when the next free slot no longer fits"; one steps a single
 * code earlier, which is what this framing uses.  Getting this wrong desyncs
 * only after the first 512-code boundary, so short members still decode and
 * the bug hides.  DELIBERATE - do not change to 0. */
#define ULEAD_LZW_WIDTH_BIAS 1U

static const uint8_t ULEAD_MAGIC[ULEAD_MAGIC_SIZE] = {
    'U', '_', 'L', 'E', 'A', 'D', ' ', 'C', 'O', 'R', 'P', '.'
};

static uint32_t ulead_read16(const uint8_t *data, size_t offset) {
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8U);
}

static uint32_t ulead_read32(const uint8_t *data, size_t offset) {
    return (uint32_t)data[offset] | ((uint32_t)data[offset + 1U] << 8U) |
           ((uint32_t)data[offset + 2U] << 16U) |
           ((uint32_t)data[offset + 3U] << 24U);
}

bool xx_ulead_parse_header(const uint8_t *input, size_t input_size,
                           uint64_t file_size, xx_ulead_header *header) {
    uint32_t count1;
    uint32_t last1;
    uint32_t count2;
    uint32_t last2;
    bool layout1;
    bool layout2;
    uint32_t layout;
    uint32_t i;

    if (!header || !input) return false;
    if ((input_size < (size_t)ULEAD_HEADER2_SIZE) ||
        (file_size < (uint64_t)ULEAD_HEADER2_SIZE)) {
        return false;
    }
    if (xx_rt_memcmp(input, ULEAD_MAGIC, ULEAD_MAGIC_SIZE) != 0) return false;

    /* Fixed words that must hold before either layout is accepted: two zero
     * dwords straddling a pair of 1s.  Every member of the family carries
     * them. */
    if (ulead_read32(input, 12U) != 0U) return false;
    if (ulead_read16(input, 16U) != 1U) return false;
    if (ulead_read16(input, 18U) != 1U) return false;
    if (ulead_read32(input, 20U) != 0U) return false;

    count1 = ulead_read16(input, 24U);
    last1 = ulead_read16(input, 26U);
    count2 = ulead_read16(input, 0x28U);
    last2 = ulead_read16(input, 0x2aU);

    layout1 = (count1 != 0U) && (last1 <= (uint32_t)XX_ULEAD_BLOCK_SIZE) &&
              (file_size > ((uint64_t)count1 * 2U +
                            (uint64_t)ULEAD_HEADER1_SIZE));
    layout2 = (count2 != 0U) && (last2 <= (uint32_t)XX_ULEAD_BLOCK_SIZE) &&
              (file_size > ((uint64_t)count2 * 2U +
                            (uint64_t)ULEAD_HEADER2_SIZE));

    layout = 0U;
    if (layout1 && !layout2) {
        layout = 1U;
    } else if (layout2 && !layout1) {
        layout = 2U;
    } else if (layout1 && layout2) {
        /* Both fit, so the tie goes on the block count: the shorter header
         * wins while its count is still plausible for one. */
        layout = (count1 < 0x2000U) ? 1U : 2U;
    }
    if (layout == 0U) return false;

    xx_rt_memset(header, 0, sizeof(*header));
    header->layout = layout;
    if (layout == 1U) {
        header->table_offset = (uint64_t)ULEAD_HEADER1_SIZE;
        header->block_count = count1;
        header->last_block_size = last1;
    } else {
        header->table_offset = (uint64_t)ULEAD_HEADER2_SIZE;
        header->block_count = count2;
        header->last_block_size = last2;
        for (i = 0U; i < (uint32_t)ULEAD_NAME_SIZE; ++i) {
            char value = (char)input[(size_t)ULEAD_NAME_OFFSET + i];
            if (value == '\0') break;
            header->file_name[i] = value;
        }
        header->file_name[i] = '\0';
    }
    header->data_offset =
        header->table_offset + ((uint64_t)header->block_count * 2U);
    if (header->data_offset > file_size) return false;
    header->uncompressed_size =
        ((uint64_t)header->block_count - 1U) * (uint64_t)XX_ULEAD_BLOCK_SIZE +
        (uint64_t)header->last_block_size;
    if (header->uncompressed_size == 0U) return false;

    return true;
}

/* ---------------------------------------------------------------------- */
/* 12-bit MSB-first LZW, one self-contained stream per block               */
/* ---------------------------------------------------------------------- */

typedef struct ulead_lzw_ctx_s {
    uint32_t *prefix; /* ULEAD_LZW_CAPACITY entries                        */
    uint8_t *suffix;  /* ULEAD_LZW_CAPACITY entries                        */
    uint8_t *stack;   /* ULEAD_LZW_STACK_SIZE entries                      */
} ulead_lzw_ctx;

typedef struct ulead_lzw_reader_s {
    const uint8_t *data;
    uint64_t bits; /* total bit count of the block payload                 */
    uint64_t bit_pos;
    uint32_t width;
    uint32_t max_code;
    uint32_t free_slot;
} ulead_lzw_reader;

static void ulead_lzw_reader_reset(ulead_lzw_reader *reader) {
    reader->width = 9U;
    reader->max_code = 0x1ffU;
    reader->free_slot = ULEAD_LZW_FIRST_FREE;
}

static bool ulead_lzw_next(ulead_lzw_reader *reader, uint32_t *code) {
    uint32_t value = 0U;
    uint32_t index;

    /* The width step is evaluated BEFORE the read, and a new entry is added
     * AFTER the phrase is emitted.  Both orderings are load bearing.
     *
     * Note that at the final width max_code is set to the CAPACITY, not to
     * capacity-1, so once the dictionary fills the reader steps once more, to
     * thirteen bits.  That is what the reference does and a full block really
     * reaches it; do not "fix" it to clamp at twelve. */
    if (reader->max_code < (reader->free_slot + ULEAD_LZW_WIDTH_BIAS)) {
        ++reader->width;
        reader->max_code = (reader->width == ULEAD_LZW_MAX_BITS)
                               ? ULEAD_LZW_CAPACITY
                               : ((1U << reader->width) - 1U);
    }

    /* A code that runs past the final byte is still readable - the encoder
     * pads the tail with zero bits - but a code that STARTS past it is not. */
    if (reader->bit_pos >= reader->bits) return false;

    for (index = 0U; index < reader->width; ++index) {
        uint64_t bit = reader->bit_pos + index;
        uint32_t set = 0U;
        if (bit < reader->bits) {
            set = (uint32_t)((reader->data[(size_t)(bit >> 3)] >>
                              (7U - (unsigned)(bit & 7U))) &
                             1U);
        }
        value = (value << 1U) | set;
    }
    reader->bit_pos += reader->width;
    *code = value;
    return true;
}

/*
 * Decode one block's LZW stream.  `room` is the block's plain size, which is
 * both the stopping rule and the buffer capacity; `produced` counts every
 * byte the stream yields, INCLUDING any that overshoot `room` and are
 * therefore discarded.  The caller demands produced == room, so an overshoot
 * is a failure - exactly as the reference, whose final
 * `baBlock.size() != nPlainSize` test rejects it.
 *
 * Returns false only for the reference's hard failures; every other stop
 * (end code, exhausted bits) is a normal break whose short output the caller
 * then rejects.
 */
static bool ulead_lzw_decode_block(ulead_lzw_ctx *ctx, const uint8_t *input,
                                   size_t input_size, uint8_t *output,
                                   size_t room, size_t *produced) {
    ulead_lzw_reader reader;
    size_t out_pos = 0U;
    size_t stack_size;
    size_t index;
    uint32_t code = 0U;
    uint32_t current;
    uint32_t guard;
    uint32_t previous = 0U;
    uint8_t previous_first = 0U;
    bool has_previous = false;

    *produced = 0U;

    /* Every block gets a fresh dictionary: the reference constructs its
     * prefix/suffix vectors inside decode(), which is called once per
     * block. */
    xx_rt_memset(ctx->prefix, 0, ULEAD_LZW_CAPACITY * sizeof(uint32_t));
    xx_rt_memset(ctx->suffix, 0, ULEAD_LZW_CAPACITY);
    for (index = 0U; index < 256U; ++index) {
        ctx->suffix[index] = (uint8_t)index;
    }

    if (input_size > (SIZE_MAX / 8U)) return false;
    reader.data = input;
    reader.bits = (uint64_t)input_size * 8U;
    reader.bit_pos = 0U;
    ulead_lzw_reader_reset(&reader);

    while (out_pos < room) {
        if (!ulead_lzw_next(&reader, &code)) break;
        if (code == ULEAD_LZW_END) break;

        if (code == ULEAD_LZW_CLEAR) {
            ulead_lzw_reader_reset(&reader);
            if (!ulead_lzw_next(&reader, &code)) break;
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
            if (code > 0xffU) return false; /* hard failure */
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
            if ((current >= ULEAD_LZW_CAPACITY) ||
                (++guard > ULEAD_LZW_CAPACITY)) {
                return false; /* hard failure */
            }
            ctx->stack[stack_size++] = ctx->suffix[current];
            current = ctx->prefix[current];
        }
        ctx->stack[stack_size++] = (uint8_t)current;

        for (index = stack_size; index > 0U; --index) {
            if (out_pos < room) output[out_pos] = ctx->stack[index - 1U];
            ++out_pos;
        }

        if (reader.free_slot < ULEAD_LZW_CAPACITY) {
            ctx->prefix[reader.free_slot] = previous;
            ctx->suffix[reader.free_slot] = (uint8_t)current;
            ++reader.free_slot;
        }
        previous = code;
        previous_first = (uint8_t)current;
    }

    *produced = out_pos;
    return true;
}

bool xx_ulead_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written) {
    xx_ulead_header header;
    ulead_lzw_ctx ctx;
    size_t position;
    size_t out_pos = 0U;
    uint32_t i;
    bool ok = true;

    if (written) *written = 0U;
    if (!input || (input_size == 0U)) return false;
    if (!output && (output_size != 0U)) return false;

    if (!xx_ulead_parse_header(input, input_size, (uint64_t)input_size,
                               &header)) {
        return false;
    }
    if (header.uncompressed_size > (uint64_t)output_size) return false;
    if ((header.table_offset + (uint64_t)header.block_count * 2U) >
        (uint64_t)input_size) {
        return false;
    }

    ctx.prefix =
        (uint32_t *)xx_mem_alloc(ULEAD_LZW_CAPACITY * sizeof(uint32_t));
    ctx.suffix = (uint8_t *)xx_mem_alloc(ULEAD_LZW_CAPACITY);
    ctx.stack = (uint8_t *)xx_mem_alloc(ULEAD_LZW_STACK_SIZE);
    if (!ctx.prefix || !ctx.suffix || !ctx.stack) {
        xx_mem_free(ctx.prefix);
        xx_mem_free(ctx.suffix);
        xx_mem_free(ctx.stack);
        return false;
    }

    position = (size_t)header.data_offset;
    for (i = 0U; ok && (i < header.block_count); ++i) {
        size_t packed_size = (size_t)ulead_read16(
            input, (size_t)header.table_offset + (size_t)i * 2U);
        size_t plain_size = (i == (header.block_count - 1U))
                                ? (size_t)header.last_block_size
                                : (size_t)XX_ULEAD_BLOCK_SIZE;

        if ((packed_size == 0U) || (position > input_size) ||
            (packed_size > (input_size - position))) {
            /* The block table sums to the file size in an intact member, so a
             * payload that runs off the end means the input is short. */
            ok = false;
            break;
        }

        if (packed_size == plain_size) {
            /* A block that did not compress is written verbatim; equal sizes
             * are the only marker for it. */
            xx_rt_memcpy(output + out_pos, input + position, plain_size);
            out_pos += plain_size;
        } else {
            size_t produced = 0U;
            if (!ulead_lzw_decode_block(&ctx, input + position, packed_size,
                                        output + out_pos, plain_size,
                                        &produced) ||
                (produced != plain_size)) {
                ok = false;
                break;
            }
            out_pos += plain_size;
        }
        position += packed_size;
    }

    xx_mem_free(ctx.prefix);
    xx_mem_free(ctx.suffix);
    xx_mem_free(ctx.stack);

    if (!ok) return false;
    if ((uint64_t)out_pos != header.uncompressed_size) return false;
    if (written) *written = out_pos;
    return true;
}
