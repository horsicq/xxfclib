/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM Personal Communications for OS/2 install-diskette payload decoder.
 * Ported from XArchive/Algos/xpcommos2decoder.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/pcommos2/xx_pcommos2.h"

/* One core routine drives both entry points, so the measure and the decode
 * can never disagree about a stream.  With output == NULL nothing is written
 * and the walk is pure bookkeeping; no sliding window is needed because a
 * PCOMM match is validated purely by position (distance against the bytes
 * produced in the current block), never by inspecting earlier data.
 *
 * NOTE (deliberate, do not "fix"): the reference performs the structural
 * block-chain validation only in scan() and the exact-size check only in
 * decode().  Here every structural check lives in the core, which cannot
 * reject anything scan() accepted -- the walks are identical -- and makes the
 * decode path strictly no laxer than the gate that feeds it. */
static bool pcommos2_run(const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t max_output,
                         size_t *produced) {
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    size_t base = 0U; /* start of the current block in the plaintext */
    /* Only the last two closed blocks need to be remembered: every earlier
     * one must be a full block, and that is checked two closes later. */
    size_t prev_size = 0U;
    size_t prev_prev_size = 0U;
    size_t block_count = 0U;

    *produced = 0U;
    if (!input || max_output == 0U) return false;

    /* Shortest possible file: one literal run of one byte, then the two
     * terminating block markers. */
    if (input_size < 4U) return false;

    /* A block always opens with a literal run - there is nothing to match
     * against yet - and the stream always closes with the two markers. */
    if (input[0] < 0xE1U) return false;
    if ((input[input_size - 1U] != 0xE0U) ||
        (input[input_size - 2U] != 0xE0U)) {
        return false;
    }

    while (in_pos < input_size) {
        uint32_t control = input[in_pos++];

        if (control >= 0xE0U) {
            size_t count = (size_t)(control - 0xE0U);

            if (count == 0U) {
                size_t block_size = out_pos - base;
                if ((block_count >= 2U) &&
                    (prev_prev_size != XX_PCOMMOS2_BLOCK_SIZE)) {
                    return false;
                }
                prev_prev_size = prev_size;
                prev_size = block_size;
                block_count++;
                base = out_pos;
                continue;
            }

            if (count > input_size - in_pos) return false;
            if (count > max_output - out_pos) return false;
            if (output) {
                xx_rt_memcpy(output + out_pos, input + in_pos, count);
            }
            in_pos += count;
            out_pos += count;
        } else if (control >= 0x20U) {
            uint32_t low;
            size_t length;
            size_t distance;
            if (in_pos >= input_size) return false;
            low = input[in_pos++];
            length = (size_t)((control >> 5U) + 2U);
            distance = (size_t)((((control & 0x1FU) << 8U) | low) + 1U);
            if (distance > out_pos - base) return false;
            if (length > max_output - out_pos) return false;
            if (output) {
                /* Byte-at-a-time on purpose: a match may overlap its own
                 * output. */
                size_t source = out_pos - distance;
                size_t i;
                for (i = 0U; i < length; i++) {
                    output[out_pos + i] = output[source + i];
                }
            }
            out_pos += length;
        } else {
            uint32_t low;
            uint32_t high;
            size_t length;
            size_t source_offset;
            if (in_pos + 1U >= input_size) return false;
            low = input[in_pos];
            high = input[in_pos + 1U];
            in_pos += 2U;
            length = (size_t)(control + 4U);
            /* ABSOLUTE offset from the start of the current block, not a
             * distance back.  Reading it as a two-byte token is the classic
             * way to get this format wrong. */
            source_offset = (size_t)(low | (high << 8U));
            if (source_offset >= out_pos - base) return false;
            if (length > max_output - out_pos) return false;
            if (output) {
                size_t source = base + source_offset;
                size_t i;
                for (i = 0U; i < length; i++) {
                    output[out_pos + i] = output[source + i];
                }
            }
            out_pos += length;
        }

        if (out_pos - base > XX_PCOMMOS2_BLOCK_SIZE) return false;
    }

    /* The stream has to end on a block boundary, with an empty terminator
     * block behind a non-empty data block. */
    if (out_pos != base) return false;
    if (block_count < 2U) return false;
    if (prev_size != 0U) return false;
    if ((prev_prev_size == 0U) ||
        (prev_prev_size > XX_PCOMMOS2_BLOCK_SIZE)) {
        return false;
    }
    if (out_pos == 0U) return false;

    *produced = out_pos;
    return true;
}

bool xx_pcommos2_scan_memory(const uint8_t *input, size_t input_size,
                             size_t max_output, size_t *consumed,
                             size_t *produced) {
    size_t out_size = 0U;
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    /* The reference caps the plaintext at a fixed 512 MiB; here max_output is
     * the only ceiling, per the library contract. */
    if (!pcommos2_run(input, input_size, NULL, max_output, &out_size)) {
        return false;
    }
    /* The walk only succeeds having consumed the whole buffer. */
    if (consumed) *consumed = input_size;
    if (produced) *produced = out_size;
    return true;
}

bool xx_pcommos2_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written) {
    size_t out_size = 0U;
    if (written) *written = 0U;
    if (!output || output_size == 0U) return false;
    if (!pcommos2_run(input, input_size, output, output_size, &out_size)) {
        return false;
    }
    /* A stream that stops short of the declared size is corrupt rather than
     * merely truncated. */
    if (out_size != output_size) return false;
    if (written) *written = out_size;
    return true;
}
