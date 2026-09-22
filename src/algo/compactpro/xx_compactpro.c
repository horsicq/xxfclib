/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compact Pro (.cpt) member decompression: the RLE-only form and the LZH form.
 *
 * Ported from XArchive's Algos/xmaclegacydecoders.cpp (XMacLegacyDecoders::
 * decodeCompactPro and the CompactLzhStream / expandCompactRle helpers in its
 * anonymous namespace), which is itself a translation of XADMaster's Compact
 * Pro handlers.
 *
 * Structure of the format, as the reference implements it:
 *
 * Both methods end in the same byte-level RLE expander. 0x81 is the escape:
 *
 *   0x81 0x82 n (n != 0)  emit the previously emitted byte n-1 more times
 *   0x81 0x82 0x00        emit 0x81, then 0x82 once (0x82 becomes "saved")
 *   0x81 0x81             emit one 0x81 and set the "half escaped" latch, so
 *                         the NEXT round starts from a synthetic 0x81 escape
 *                         lead and consumes the following stored byte as that
 *                         escape's argument. It does not emit two 0x81s and
 *                         it does not consume a byte for the second 0x81.
 *   0x81 b                emit 0x81 then b
 *   b                     emit b
 *
 * For the LZH method the bytes handed to that expander are not the stored
 * bytes but the output of an LZH stream: a sequence of blocks, each of which
 * begins with three Huffman tables (literal/256, length/64, offset-high/128)
 * whose code lengths are stored as packed nibbles, followed by tokens:
 *
 *   1 <literal>                        a literal byte
 *   0 <length> <offset-high> <6 bits>  a match of `length` bytes at distance
 *                                      (offset-high << 6 | 6 bits) in an
 *                                      8 KiB circular window
 *
 * A block ends by a cost counter, not a symbol count: every literal charges 2
 * and every match charges 3, and when the running total reaches the block size
 * the next token starts a new block. That is deliberate and load-bearing -- a
 * match charges 3 once regardless of how many bytes it expands to.
 *
 * One deviation from the reference, which does not change the bytes produced:
 * the Huffman tables are decoded canonically (per-length counts plus a
 * symbol list) instead of through the reference's explicitly built binary
 * tree. The reference assigns codes by walking lengths 1..max and, within a
 * length, symbols in increasing order, incrementing the code and shifting left
 * between lengths -- that is the definition of a canonical code, so the same
 * bits select the same symbol. The reference's rejection of an over-subscribed
 * table (its `code >= (1 << length)` test) is reproduced by the Kraft check in
 * cp_code_build; like the reference, an INCOMPLETE table is accepted and only
 * fails if the stream actually asks for an unassigned code.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/compactpro/xx_compactpro.h"

#define CP_WINDOW_SIZE 8192U
#define CP_WINDOW_MASK 8191U
#define CP_MAX_CODE_LENGTH 15
#define CP_MAX_SYMBOLS 256

/* ------------------------------------------------------------------ bits */

typedef struct cp_bits {
    const uint8_t *data;
    size_t size;
    size_t bitpos;
    bool ok;
} cp_bits;

static uint32_t cp_bits_read(cp_bits *r, unsigned count)
{
    uint32_t value = 0;
    unsigned i;
    if (!r->ok || count > 32U) {
        r->ok = false;
        return 0;
    }
    for (i = 0; i < count; ++i) {
        size_t index = r->bitpos >> 3;
        if (index >= r->size) {
            r->ok = false;
            return 0;
        }
        value = (value << 1) |
                ((uint32_t)(r->data[index] >> (7U - (unsigned)(r->bitpos & 7U))) & 1U);
        ++r->bitpos;
    }
    return value;
}

static uint8_t cp_bits_take_byte(cp_bits *r)
{
    uint8_t value;
    if (!r->ok || (r->bitpos & 7U) != 0U || (r->bitpos >> 3) >= r->size) {
        r->ok = false;
        return 0;
    }
    value = r->data[r->bitpos >> 3];
    r->bitpos += 8U;
    return value;
}

static void cp_bits_align(cp_bits *r)
{
    if (r->ok) r->bitpos = (r->bitpos + 7U) & ~(size_t)7U;
}

static size_t cp_bits_aligned_offset(const cp_bits *r)
{
    return r->bitpos >> 3;
}

static void cp_bits_skip_bytes(cp_bits *r, size_t count)
{
    if (!r->ok || (r->bitpos & 7U) != 0U || count > r->size ||
        (r->bitpos >> 3) > r->size - count) {
        r->ok = false;
        return;
    }
    r->bitpos += count * 8U;
}

/* --------------------------------------------------------------- huffman */

typedef struct cp_code {
    uint16_t count[CP_MAX_CODE_LENGTH + 1];
    uint16_t symbol[CP_MAX_SYMBOLS];
    bool valid;
} cp_code;

/* lengths[] holds one code length per symbol, 0 meaning "absent". */
static bool cp_code_build(cp_code *code, const uint8_t *lengths, int size)
{
    int offsets[CP_MAX_CODE_LENGTH + 2];
    int total = 0;
    int left = 1;
    int length;
    int symbol;

    code->valid = false;
    xx_rt_memset(code->count, 0, sizeof(code->count));

    if (size <= 0 || size > CP_MAX_SYMBOLS) return false;

    for (symbol = 0; symbol < size; ++symbol) {
        int len = (int)lengths[symbol];
        if (len > CP_MAX_CODE_LENGTH) return false;
        if (len > 0) {
            ++code->count[len];
            ++total;
        }
    }
    if (total == 0) return false;

    /* Kraft: reject an over-subscribed table exactly where the reference's
     * `code >= (1 << length)` test would. An incomplete table (left > 0 at the
     * end) is accepted, as in the reference. */
    for (length = 1; length <= CP_MAX_CODE_LENGTH; ++length) {
        left <<= 1;
        left -= (int)code->count[length];
        if (left < 0) return false;
    }

    offsets[1] = 0;
    for (length = 1; length <= CP_MAX_CODE_LENGTH; ++length)
        offsets[length + 1] = offsets[length] + (int)code->count[length];

    for (symbol = 0; symbol < size; ++symbol) {
        int len = (int)lengths[symbol];
        if (len > 0) code->symbol[offsets[len]++] = (uint16_t)symbol;
    }

    code->valid = true;
    return true;
}

/* Returns the decoded symbol, or -1 on a bad stream / unassigned code. */
static int cp_code_decode(const cp_code *code, cp_bits *r)
{
    int first = 0;
    int index = 0;
    int codeval = 0;
    int length;

    if (!code->valid) return -1;
    for (length = 1; length <= CP_MAX_CODE_LENGTH; ++length) {
        int count;
        codeval |= (int)cp_bits_read(r, 1);
        if (!r->ok) return -1;
        count = (int)code->count[length];
        if (codeval - first < count)
            return (int)code->symbol[index + (codeval - first)];
        index += count;
        first = (first + count) << 1;
        codeval <<= 1;
    }
    return -1;
}

/* A Compact Pro table: one byte of pair count, then that many bytes each
 * holding two 4-bit code lengths. */
static bool cp_read_table(cp_bits *r, int size, cp_code *code)
{
    uint8_t lengths[CP_MAX_SYMBOLS];
    int pairs;
    int i;

    if (size <= 0 || size > CP_MAX_SYMBOLS || (r->bitpos & 7U) != 0U)
        return false;
    pairs = (int)cp_bits_take_byte(r);
    if (!r->ok || pairs * 2 > size) return false;
    xx_rt_memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < pairs; ++i) {
        uint8_t value = cp_bits_take_byte(r);
        if (!r->ok) return false;
        lengths[2 * i] = (uint8_t)(value >> 4);
        lengths[2 * i + 1] = (uint8_t)(value & 15U);
    }
    return cp_code_build(code, lengths, size);
}

/* ------------------------------------------------------------ lzh stream */

typedef struct cp_lzh {
    cp_bits reader;
    uint32_t block_size;
    uint32_t block_count;
    size_t block_start;
    uint32_t position;
    int match_length;
    uint32_t match_offset;
    cp_code literal;
    cp_code length;
    cp_code offset;
    uint8_t window[CP_WINDOW_SIZE];
} cp_lzh;

static bool cp_lzh_start_block(cp_lzh *s)
{
    if (s->block_start != 0) {
        size_t current;
        cp_bits_align(&s->reader);
        current = cp_bits_aligned_offset(&s->reader);
        if (current < s->block_start) return false;
        /* Deliberate, load-bearing: Compact Pro pads a block to an even byte
         * boundary relative to where the previous block's data began and then
         * writes a two-byte marker, so the gap is 3 bytes when the block's
         * length is odd and 2 when it is even. Do not "simplify" this. */
        cp_bits_skip_bytes(&s->reader,
                           ((current - s->block_start) & 1U) ? 3U : 2U);
        if (!s->reader.ok) return false;
    }
    if (!cp_read_table(&s->reader, 256, &s->literal) ||
        !cp_read_table(&s->reader, 64, &s->length) ||
        !cp_read_table(&s->reader, 128, &s->offset))
        return false;
    s->block_count = 0;
    s->block_start = cp_bits_aligned_offset(&s->reader);
    return true;
}

static bool cp_lzh_next(cp_lzh *s, uint8_t *value)
{
    if (!s->reader.ok) return false;

    if (s->match_length > 0) {
        *value = s->window[s->match_offset & CP_WINDOW_MASK];
        ++s->match_offset;
        --s->match_length;
    } else {
        if (s->block_count >= s->block_size && !cp_lzh_start_block(s))
            return false;
        if (cp_bits_read(&s->reader, 1)) {
            int literal;
            if (!s->reader.ok) return false;
            s->block_count += 2U;
            literal = cp_code_decode(&s->literal, &s->reader);
            if (literal < 0 || literal > 255) return false;
            *value = (uint8_t)literal;
        } else {
            int length;
            int high;
            uint32_t offset;
            if (!s->reader.ok) return false;
            s->block_count += 3U;
            length = cp_code_decode(&s->length, &s->reader);
            high = cp_code_decode(&s->offset, &s->reader);
            if (length < 0 || high < 0 || high > 127) return false;
            offset = ((uint32_t)high << 6) | cp_bits_read(&s->reader, 6);
            if (!s->reader.ok || length <= 0 || length > 63 || offset > 8191U)
                return false;
            s->match_length = length;
            /* Modular, exactly as the reference: it computes position -
             * offset in a 32-bit int (which may go negative near the start of
             * the stream, or alias the byte about to be written when offset
             * is 0) and masks with 8191. Such a reference is not rejected --
             * it reads the window's initial zeroes or a stale byte. Unsigned
             * wraparound here reproduces the same index bit for bit. */
            s->match_offset = s->position - offset;
            *value = s->window[s->match_offset & CP_WINDOW_MASK];
            ++s->match_offset;
            --s->match_length;
        }
    }
    s->window[s->position & CP_WINDOW_MASK] = *value;
    ++s->position;
    return true;
}

/* ------------------------------------------------------------------- rle */

typedef struct cp_source {
    bool lzh;
    const uint8_t *data; /* raw form */
    size_t size;
    size_t pos;
    cp_lzh *stream; /* lzh form */
} cp_source;

static bool cp_source_next(cp_source *src, uint8_t *value)
{
    if (src->lzh) return cp_lzh_next(src->stream, value);
    if (src->pos >= src->size) return false;
    *value = src->data[src->pos++];
    return true;
}

static bool cp_expand_rle(cp_source *src, uint8_t *output, size_t output_size,
                          size_t *produced)
{
    size_t written = 0;
    uint8_t saved = 0;
    /* Signed on purpose: see the `byte == 1` case below. */
    int32_t repeat = 0;
    bool half_escaped = false;

    while (written < output_size) {
        uint8_t byte;
        if (repeat != 0) {
            output[written++] = saved;
            --repeat;
            continue;
        }
        if (half_escaped) {
            byte = 0x81U;
            half_escaped = false;
        } else if (!cp_source_next(src, &byte)) {
            *produced = written;
            return false;
        }
        if (byte != 0x81U) {
            saved = byte;
            output[written++] = byte;
            continue;
        }
        if (!cp_source_next(src, &byte)) {
            *produced = written;
            return false;
        }
        if (byte == 0x82U) {
            if (!cp_source_next(src, &byte)) {
                *produced = written;
                return false;
            }
            if (byte) {
                /* byte == 1 yields repeat == -1. The reference tests `if
                 * (repeat)`, not `> 0`, so a count of 1 means "fill the rest
                 * of the member with the saved byte": the counter keeps going
                 * negative and the loop ends only when the declared plaintext
                 * size is reached. Bounded by output_size, so it is kept as
                 * the reference has it rather than rejected. */
                repeat = (int32_t)byte - 2;
                output[written++] = saved;
            } else {
                repeat = 1;
                saved = 0x82U;
                output[written++] = 0x81U;
            }
        } else if (byte == 0x81U) {
            half_escaped = true;
            saved = 0x81U;
            output[written++] = 0x81U;
        } else {
            repeat = 1;
            saved = byte;
            output[written++] = 0x81U;
        }
    }
    *produced = written;
    return written == output_size;
}

/* ---------------------------------------------------------------- public */

XXFC_API bool xx_compactpro_decode_memory(const uint8_t *input,
                                          size_t input_size, bool lzh,
                                          uint32_t block_size,
                                          uint8_t *output, size_t output_size,
                                          size_t *written)
{
    cp_source source;
    cp_lzh *stream = 0;
    size_t produced = 0;
    bool result;

    if (written) *written = 0;
    if (!input || input_size == 0) return false;
    if (output_size != 0 && !output) return false;

    source.lzh = lzh;
    source.data = input;
    source.size = input_size;
    source.pos = 0;
    source.stream = 0;

    if (lzh) {
        if (block_size == 0) return false;
        stream = (cp_lzh *)xx_mem_alloc(sizeof(cp_lzh));
        if (!stream) return false;
        xx_rt_memset(stream, 0, sizeof(cp_lzh));
        stream->reader.data = input;
        stream->reader.size = input_size;
        stream->reader.bitpos = 0;
        stream->reader.ok = true;
        stream->block_size = block_size;
        /* Starts "full" so the very first token opens a block. */
        stream->block_count = block_size;
        source.stream = stream;
    }

    result = cp_expand_rle(&source, output, output_size, &produced);

    if (stream) xx_mem_free(stream);
    if (written) *written = result ? produced : 0;
    return result;
}

XXFC_API bool xx_compactpro_rle_decode_memory(const uint8_t *input,
                                              size_t input_size,
                                              uint8_t *output,
                                              size_t output_size,
                                              size_t *written)
{
    return xx_compactpro_decode_memory(input, input_size, false, 0, output,
                                       output_size, written);
}

XXFC_API bool xx_compactpro_lzh_decode_memory(const uint8_t *input,
                                              size_t input_size,
                                              uint8_t *output,
                                              size_t output_size,
                                              size_t *written)
{
    return xx_compactpro_decode_memory(input, input_size, true,
                                       XX_COMPACTPRO_BLOCK_SIZE, output,
                                       output_size, written);
}
