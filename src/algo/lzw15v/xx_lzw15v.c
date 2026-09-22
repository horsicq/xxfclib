/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Mark Nelson's variable-width LZW (9..15 bits) as a headerless stream:
 * a port of XRawLzw15vDecoder, keeping its exact control-code grammar.
 */
#include "xxfclib/algo/lzw15v/xx_lzw15v.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* MSB-first bit pump.  A byte is taken from the input WHOLE and drained from
 * bit 7 down, so `offset` only ever advances on a byte boundary - which is
 * what makes "the stream occupies exactly N input bytes" a meaningful test for
 * a format that has nothing else to check.  Unlike an LSB-first cache there is
 * no partially-buffered byte to subtract back out: a byte whose bits are still
 * being drained HAS been consumed by the stream. */
typedef struct lzw15v_bits {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint8_t current;
    int bits_left;
} lzw15v_bits;

/* The dictionary is 128 KiB of tables; it is heap-allocated per call and owned
 * by the caller of lzw15v_run, never module-level state. */
typedef struct lzw15v_tables {
    uint16_t prefix[XX_LZW15V_MAX_CODES];
    uint8_t append[XX_LZW15V_MAX_CODES];
    uint8_t stack[XX_LZW15V_MAX_CODES];
} lzw15v_tables;

static bool lzw15v_read(lzw15v_bits *reader, int need, uint32_t *value) {
    uint32_t result = 0U;
    int i;

    for (i = 0; i < need; ++i) {
        if (reader->bits_left == 0) {
            if (reader->offset >= reader->input_size) return false;
            reader->current = reader->input[reader->offset];
            ++reader->offset;
            reader->bits_left = 8;
        }
        result = (result << 1) | (uint32_t)((reader->current >> 7) & 0x01U);
        reader->current = (uint8_t)((reader->current << 1) & 0xFFU);
        --reader->bits_left;
    }

    *value = result;
    return true;
}

/* The single implementation behind both entry points.  `output` NULL means
 * measure only: LZW rebuilds every string from the dictionary rather than from
 * already-emitted output, so discarding the bytes cannot change the decode -
 * no sliding window is needed for the measuring path to agree with the real
 * one.  `strict` adds the grammar checks detection needs and that the original
 * decoder deliberately does not perform. */
static bool lzw15v_run(const uint8_t *input, size_t input_size, bool strict,
                       uint8_t *output, size_t limit, lzw15v_tables *tables,
                       size_t *produced_out, size_t *consumed_out) {
    lzw15v_bits reader;
    size_t produced = 0U;
    bool end_seen = false;
    uint32_t i;

    if (!input || (input_size == 0U) || (limit == 0U) || !tables) return false;

    reader.input = input;
    reader.input_size = input_size;
    reader.offset = 0U;
    reader.current = 0U;
    reader.bits_left = 0;

    xx_rt_memset(tables->prefix, 0, sizeof(tables->prefix));
    xx_rt_memset(tables->append, 0, sizeof(tables->append));
    for (i = 0U; i < 256U; ++i) {
        tables->append[i] = (uint8_t)i;
    }

    while (!end_seen) {
        /* Stream opening, and the state a CLEAR returns to: a bare 9-bit code
         * emitted as a literal, which seeds the previous-code chain. */
        uint32_t code = 0U;
        uint32_t next_code;
        uint32_t previous_code;
        uint32_t character;
        int code_bits;
        bool cleared = false;

        if (!lzw15v_read(&reader, XX_LZW15V_MIN_CODE_BITS, &code)) return false;
        if (code == XX_LZW15V_CODE_END) {
            end_seen = true;
            break;
        }
        if (strict && (code > 0xFFU)) return false;

        if (produced >= limit) return false;
        /* Non-strict deliberately masks rather than rejects an opening code in
         * 0x103..0x1FF, exactly as the reference does. */
        if (output) output[produced] = (uint8_t)(code & 0xFFU);
        ++produced;

        next_code = XX_LZW15V_FIRST_CODE;
        code_bits = XX_LZW15V_MIN_CODE_BITS;
        previous_code = code;
        character = code;

        while (!cleared) {
            uint32_t new_code = 0U;
            uint32_t walk;
            size_t stack_size;

            if (!lzw15v_read(&reader, code_bits, &new_code)) return false;

            if (new_code == XX_LZW15V_CODE_END) {
                end_seen = true;
                break;
            }

            if (new_code == XX_LZW15V_CODE_BUMP) {
                /* The width is driven ONLY by this code.  A real stream never
                 * emits it at the cap; non-strict swallows it silently instead
                 * of failing - deliberate, matches the reference. */
                if (strict && (code_bits >= XX_LZW15V_MAX_CODE_BITS)) {
                    return false;
                }
                if (code_bits < XX_LZW15V_MAX_CODE_BITS) ++code_bits;
                continue;
            }

            if (new_code == XX_LZW15V_CODE_CLEAR) {
                cleared = true;
                break;
            }

            if (strict) {
                if (new_code > next_code) return false;
                /* ">" and not ">=": a next_code exactly equal to 1 << bits is
                 * legal because the encoder emits BUMP before the code that
                 * would need the extra bit.  Deliberate. */
                if (next_code > (1U << code_bits)) return false;
            }

            stack_size = 0U;

            if (new_code >= next_code) {
                /* KwKwK: the code about to be defined is already in use. */
                tables->stack[stack_size] = (uint8_t)(character & 0xFFU);
                ++stack_size;
                walk = previous_code;
            } else {
                walk = new_code;
            }

            while (walk > 0xFFU) {
                if (walk >= XX_LZW15V_MAX_CODES) return false;
                if (stack_size >= (size_t)XX_LZW15V_MAX_CODES) return false;
                tables->stack[stack_size] = tables->append[walk];
                ++stack_size;
                walk = tables->prefix[walk];
            }

            if (stack_size >= (size_t)XX_LZW15V_MAX_CODES) return false;
            tables->stack[stack_size] = (uint8_t)(walk & 0xFFU);
            ++stack_size;

            character = walk;

            /* Running out of room is a failure, never a truncation. */
            if (stack_size > (limit - produced)) return false;

            while (stack_size > 0U) {
                --stack_size;
                if (output) output[produced] = tables->stack[stack_size];
                ++produced;
            }

            if (next_code < XX_LZW15V_MAX_CODES) {
                tables->prefix[next_code] = (uint16_t)previous_code;
                tables->append[next_code] = (uint8_t)(character & 0xFFU);
                ++next_code;
            }

            previous_code = new_code;
        }
    }

    if (!end_seen) return false;

    if (produced_out) *produced_out = produced;
    if (consumed_out) *consumed_out = reader.offset;
    return true;
}

bool xx_lzw15v_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written) {
    lzw15v_tables *tables;
    size_t produced = 0U;
    bool result;

    if (written) *written = 0U;
    if (!output || (output_size == 0U)) return false;

    tables = (lzw15v_tables *)xx_mem_alloc(sizeof(lzw15v_tables));
    if (!tables) return false;

    result = lzw15v_run(input, input_size, false, output, output_size, tables,
                        &produced, NULL);
    xx_mem_free(tables);

    if (!result) return false;
    /* The containers demand an exact fill, as XRawLzw15vDecoder::decode does:
     * fewer bytes than expected is a failed decode, not a short one. */
    if (produced != output_size) return false;

    if (written) *written = produced;
    return true;
}

bool xx_lzw15v_scan_memory(const uint8_t *input, size_t input_size,
                           size_t max_output, size_t *consumed,
                           size_t *produced) {
    lzw15v_tables *tables;
    size_t produced_local = 0U;
    size_t consumed_local = 0U;
    bool result;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;

    tables = (lzw15v_tables *)xx_mem_alloc(sizeof(lzw15v_tables));
    if (!tables) return false;

    result = lzw15v_run(input, input_size, true, NULL, max_output, tables,
                        &produced_local, &consumed_local);
    xx_mem_free(tables);

    if (!result) return false;

    if (consumed) *consumed = consumed_local;
    if (produced) *produced = produced_local;
    return true;
}
