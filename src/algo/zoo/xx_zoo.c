/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZOO member codecs: method 1 ("lzd") and method 2 ("lzh").
 *
 * Method 1 is Rahul Dhesi's LZD, a variable-width LZW:
 *
 *   - codes are packed least-significant-bit first, low bits of a byte first;
 *   - the width starts at 9 and grows to at most 13 bits;
 *   - code 256 clears the table, code 257 ends the stream;
 *   - the first free code is 258 and the table holds 8192 entries.
 *
 * It differs from the other LZW dialects this library already carries: TIFF
 * and PDF pack MSB-first and widen one code early, and UNIX compress has a
 * two-byte magic and block-mode bookkeeping. Nothing is shared.
 *
 * Method 2 is the LHA -lh5- algorithm and forwards to that decoder; see
 * xx_zoo_lzh_decode_memory() below.
 *
 * Both entry points take the exact decoded size, because every ZOO directory
 * entry stores it (field "org_size" at offset 20 of the entry). There is no
 * scanning entry point for that reason.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/zoo/xx_zoo.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_ZOO_LZD_CLEAR 256U
#define XX_ZOO_LZD_EOF 257U
#define XX_ZOO_LZD_FIRST_FREE 258U
#define XX_ZOO_LZD_TABLE_SIZE 8192U
#define XX_ZOO_LZD_MIN_BITS 9
#define XX_ZOO_LZD_MAX_BITS 13

typedef struct xx_zoo_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position; /* next byte to load */
    uint64_t cache;  /* bits not yet consumed, lowest bit first */
    int available;   /* how many bits sit in cache */
} xx_zoo_bits;

/* Tables are 32 KiB together, so they are heap allocated rather than put on
 * the caller's stack. They live in this struct and nowhere else: no module
 * level state, so the decoder is reentrant. */
typedef struct xx_zoo_lzd_tables_s {
    uint16_t prefix[XX_ZOO_LZD_TABLE_SIZE];
    uint8_t suffix[XX_ZOO_LZD_TABLE_SIZE];
    uint8_t stack[XX_ZOO_LZD_TABLE_SIZE];
} xx_zoo_lzd_tables;

/*
 * Running out of input is a hard failure here, not zero padding: LZD is
 * framed by its explicit end code, so a code that cannot be filled means the
 * member was truncated.
 */
static bool xx_zoo_read_code(xx_zoo_bits *bits, int width, uint32_t *code) {
    while (bits->available < width) {
        if (bits->position >= bits->size) return false;
        bits->cache |= ((uint64_t)bits->data[bits->position++])
                       << bits->available;
        bits->available += 8;
    }
    *code = (uint32_t)(bits->cache & ((((uint64_t)1U) << width) - 1U));
    bits->cache >>= width;
    bits->available -= width;
    return true;
}

/*
 * Walk a code back to its root, pushing suffix bytes. The stack comes out
 * reversed, so the caller emits it from the top down.
 *
 * Every step is bounded: a prefix link must point at an already-defined code
 * (< next_code), the stack can never exceed the table, and the walk itself is
 * depth limited. A forward or self reference is a malformed stream and fails
 * rather than looping.
 */
static bool xx_zoo_lzd_expand(uint32_t code, uint32_t next_code,
                              const xx_zoo_lzd_tables *tables,
                              uint8_t *stack, uint32_t *stack_size,
                              uint8_t *first_byte) {
    uint32_t size = 0U;
    uint32_t depth = 0U;

    while (code >= XX_ZOO_LZD_FIRST_FREE) {
        if ((code >= next_code) || (size >= XX_ZOO_LZD_TABLE_SIZE) ||
            (++depth > XX_ZOO_LZD_TABLE_SIZE)) {
            return false;
        }
        stack[size++] = tables->suffix[code];
        code = tables->prefix[code];
    }
    /* The root must be a literal. Codes 256 and 257 are the control codes and
     * are never valid as a prefix link. */
    if ((code >= 256U) || (size >= XX_ZOO_LZD_TABLE_SIZE)) return false;
    stack[size++] = (uint8_t)code;
    *stack_size = size;
    *first_byte = (uint8_t)code;
    return true;
}

bool xx_zoo_lzd_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    xx_zoo_bits bits;
    xx_zoo_lzd_tables *tables;
    size_t produced = 0U;
    uint32_t next_code = XX_ZOO_LZD_FIRST_FREE;
    uint32_t width_limit = 1U << XX_ZOO_LZD_MIN_BITS;
    int width = XX_ZOO_LZD_MIN_BITS;
    long previous_code = -1; /* -1 means "no previous code yet" */
    bool initial_clear_seen = false;
    bool eof_seen = false;
    bool ok = false;

    if (written) *written = 0U;
    if (!input) return false;
    if (!output && (output_size != 0U)) return false;

    tables = (xx_zoo_lzd_tables *)xx_mem_alloc(sizeof(*tables));
    if (!tables) return false;
    xx_mem_zero(tables, sizeof(*tables));

    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.cache = 0U;
    bits.available = 0;

    for (;;) {
        uint32_t code = 0U;
        uint32_t input_code;
        uint32_t stack_size = 0U;
        uint8_t first_byte = 0U;

        if (!xx_zoo_read_code(&bits, width, &code)) goto done;

        /* The stream must open with a clear code. An LZD member that starts
         * any other way is not one. Note the reference does not "continue"
         * here: the opening clear falls through into the clear handling
         * below, which is what primes the first literal. */
        if (!initial_clear_seen) {
            if (code != XX_ZOO_LZD_CLEAR) goto done;
            initial_clear_seen = true;
        }

        if (code == XX_ZOO_LZD_EOF) {
            eof_seen = true;
            break;
        }

        if (code == XX_ZOO_LZD_CLEAR) {
            width = XX_ZOO_LZD_MIN_BITS;
            width_limit = 1U << XX_ZOO_LZD_MIN_BITS;
            next_code = XX_ZOO_LZD_FIRST_FREE;
            previous_code = -1;

            /* A clear is always followed immediately by a bare literal (or by
             * the end code), read at the reset width. This is deliberate: the
             * encoder emits the first character of the new table generation
             * as a plain code, and nothing is added to the table for it. */
            if (!xx_zoo_read_code(&bits, width, &code)) goto done;
            if (code == XX_ZOO_LZD_EOF) {
                eof_seen = true;
                break;
            }
            if (code >= 256U) goto done;
            if (produced >= output_size) goto done;
            output[produced++] = (uint8_t)code;
            previous_code = (long)code;
            continue;
        }

        /* Once the table is full the only legal next code is a clear, so a
         * data code here is an error. The reference checks this before
         * decoding the code, i.e. the code that fills entry 8191 is itself
         * accepted and the one after it fails; keep that ordering. */
        if ((previous_code < 0) || (next_code >= XX_ZOO_LZD_TABLE_SIZE)) {
            goto done;
        }

        input_code = code;

        if (code < next_code) {
            if (!xx_zoo_lzd_expand(code, next_code, tables, tables->stack,
                                   &stack_size, &first_byte)) {
                goto done;
            }
        } else if (code == next_code) {
            /* The KwKwK case: the code being defined right now. Expand the
             * previous code and append its own first byte. */
            if (!xx_zoo_lzd_expand((uint32_t)previous_code, next_code, tables,
                                   tables->stack, &stack_size, &first_byte)) {
                goto done;
            }
        } else {
            goto done;
        }

        while (stack_size > 0U) {
            if (produced >= output_size) goto done;
            output[produced++] = tables->stack[--stack_size];
        }
        if (code == next_code) {
            if (produced >= output_size) goto done;
            output[produced++] = first_byte;
        }

        tables->prefix[next_code] = (uint16_t)previous_code;
        tables->suffix[next_code] = first_byte;
        next_code++;
        /*
         * Widen only after the entry is added, and compare against the full
         * 1 << width with ">=". Do not "fix" this to ">" or to an early
         * change: the encoder and the decoder are deliberately asymmetric.
         * The encoder writes a code and only then adds its entry, so while
         * coding a given code it is one entry ahead of the decoder, and it
         * widens on ">" (free_code strictly past 1 << width). One ahead plus
         * one stricter means both change width on exactly the same code.
         * Round-tripping against an encoder built to that rule confirms it;
         * an encoder using ">=" here desynchronises at the very first
         * boundary (code 255, the 9 -> 10 bit change).
         */
        if ((next_code >= width_limit) && (width < XX_ZOO_LZD_MAX_BITS)) {
            width++;
            width_limit <<= 1;
        }
        previous_code = (long)input_code;
    }

    /*
     * Only the spare bits of the byte that carried the end code belong to the
     * stream, and they must be zero. Whole trailing bytes are container data:
     * a member whose compressed size covers more than the stream is malformed,
     * so require the input to be exactly consumed. And the decoded length must
     * match the size the directory entry promised -- a short decode reported
     * as success is exactly what a caller cannot detect.
     */
    ok = initial_clear_seen && eof_seen && (bits.cache == 0U) &&
         (bits.position == input_size) && (produced == output_size);

done:
    xx_mem_free(tables);
    if (ok) {
        if (written) *written = produced;
        return true;
    }
    return false;
}

/*
 * ZOO method 2 is not a separate algorithm: it is LHA -lh5- (both come from
 * ar002), 8 KiB window, same pre-table / literal-table / position-table
 * layout and the same block framing. The reference reader confirms this --
 * it dispatches ZOO's method 2 straight into the -lh5- decoder, asking only
 * for a stricter end-of-stream rule.
 *
 * That stricter rule is that ZOO's encoder writes an explicit zero-sized
 * final block, and the reference insists on seeing it. It is a validation
 * step, not a decoding step: it produces no bytes. Here the decoded size is
 * always known from the directory entry, so decoding stops exactly at
 * output_size and the trailing zero block is never read. Nothing is lost in
 * the bytes produced; what is lost is one extra corruption check, which a
 * reader can still make by comparing the entry's CRC16.
 */
bool xx_zoo_lzh_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    return xx_lzh5_decode_memory(input, input_size, output, output_size, 5,
                                 written);
}
