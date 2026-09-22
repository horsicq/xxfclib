/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QDA byte-pair-encoding decoder, ported from XArchive/Algos/xqdadecoder.cpp.
 *
 * Stream layout, per block:
 *   - a pair table, built from control bytes: a control byte above 0x7f skips
 *     (control - 0x7f) table slots, leaving them at their identity value;
 *     otherwise it introduces (control + 1) consecutive entries, each one a
 *     "left" byte plus, when that left byte differs from the entry's own
 *     index, a "right" byte.
 *   - a 32-bit little-endian count of packed bytes.
 *   - that many packed bytes, expanded through the table with a 256-entry
 *     stack: a byte equal to its own left entry is a literal, anything else
 *     pushes right then left.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/qda/xx_qda.h"

#define QDA_ALPHABET_SIZE 0x100
#define QDA_SKIP_BIAS 0x7f
#define QDA_STACK_SIZE 0x100
#define QDA_STACK_LIMIT 0xfe

bool xx_qda_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written)
{
    uint8_t left[QDA_ALPHABET_SIZE];
    uint8_t right[QDA_ALPHABET_SIZE];
    uint8_t stack[QDA_STACK_SIZE];
    size_t position = 0;
    size_t produced = 0;
    int i;

    if (written) *written = 0;
    if (!input && input_size) return false;
    if (!output && output_size) return false;

    /* Deliberate, and load-bearing: the right table is NOT reset per block,
     * only the left one is.  A block may reference a right byte installed by
     * an earlier block's table. */
    for (i = 0; i < QDA_ALPHABET_SIZE; ++i) right[i] = 0;

    while (position < input_size) {
        int code = 0;
        uint32_t block_length;
        int stack_depth = 0;

        for (i = 0; i < QDA_ALPHABET_SIZE; ++i) left[i] = (uint8_t)i;

        for (;;) {
            int control;
            int k;

            if (position >= input_size) return false;
            control = input[position];
            ++position;
            if (control > QDA_SKIP_BIAS) {
                code += control - QDA_SKIP_BIAS;
                control = 0;
            }
            if (code == QDA_ALPHABET_SIZE) break;

            for (k = 0; k <= control; ++k) {
                if ((code > (QDA_ALPHABET_SIZE - 1)) ||
                    (position >= input_size)) {
                    return false;
                }
                left[code] = input[position];
                ++position;
                if ((uint8_t)code != left[code]) {
                    if (position >= input_size) return false;
                    right[code] = input[position];
                    ++position;
                }
                ++code;
            }
            /* The completion test is made twice on purpose - once right after
             * the skip above and once here; a table that fills on its last
             * entry run never reaches another control byte. */
            if (code == QDA_ALPHABET_SIZE) break;
        }

        if ((input_size - position) < 4) return false;
        block_length = (uint32_t)input[position] |
                       ((uint32_t)input[position + 1] << 8) |
                       ((uint32_t)input[position + 2] << 16) |
                       ((uint32_t)input[position + 3] << 24);
        position += 4;

        for (;;) {
            uint8_t symbol;

            if (stack_depth == 0) {
                if (block_length == 0) break;
                --block_length;
                if (position >= input_size) return false;
                symbol = input[position];
                ++position;
            } else {
                --stack_depth;
                symbol = stack[stack_depth];
            }

            if (symbol == left[symbol]) {
                /* The reference emits first and only then notices it has
                 * passed the declared length, so an overlong member is a
                 * failed member rather than a truncated one.  output_size is
                 * that declared length. */
                if (produced >= output_size) return false;
                output[produced] = symbol;
                ++produced;
            } else {
                if (stack_depth > QDA_STACK_LIMIT) return false;
                stack[stack_depth] = right[symbol];
                stack[stack_depth + 1] = left[symbol];
                stack_depth += 2;
            }
        }
    }

    if (written) *written = produced;
    return true;
}
