/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_bzip2_internal.h"
#include "xxfclib/rt/xx_rt.h"

/* Rebuild an ordinary Huffman tree after reducing weight differences if its
 * longest code exceeds the format limit. Clipping individual depths would
 * violate the Kraft bound and create overlapping canonical codes. Reducing
 * all positive weights preserves a real prefix tree on every retry; once
 * weights converge to 1 or 2 the tree fits comfortably within 20 bits.
 */
bool xx_bzip2_huffman_lengths(const uint32_t *freq, int symbol_count, uint8_t *lengths)
{
    uint32_t weights[BZ2_MAX_ALPHA_SIZE];
    uint64_t node_weights[2 * BZ2_MAX_ALPHA_SIZE - 1];
    int16_t parents[2 * BZ2_MAX_ALPHA_SIZE - 1];
    uint8_t candidate[BZ2_MAX_ALPHA_SIZE];
    int symbol;

    if (!freq || !lengths || symbol_count <= 0 || symbol_count > BZ2_MAX_ALPHA_SIZE) return false;
    for (symbol = 0; symbol < symbol_count; ++symbol) weights[symbol] = freq[symbol] != 0U ? freq[symbol] : 1U;

    for (;;) {
        int active = symbol_count;
        int node_count = symbol_count;
        bool too_deep = false;
        for (symbol = 0; symbol < symbol_count; ++symbol) {
            node_weights[symbol] = weights[symbol];
            parents[symbol] = -1;
        }

        /* Match the original encoder's first-minimum tie ordering. A wide
         * sum accepts arbitrary uint32_t leaf weights without overflowing:
         * there are at most 258 leaves, so no sum can reach 2^41. */
        while (active > 1) {
            int min1 = -1, min2 = -1;
            int node;
            for (node = 0; node < node_count; ++node) {
                if (node_weights[node] == 0U) continue;
                if (min1 < 0 || node_weights[node] < node_weights[min1]) {
                    min2 = min1;
                    min1 = node;
                } else if (min2 < 0 || node_weights[node] < node_weights[min2]) {
                    min2 = node;
                }
            }
            node_weights[node_count] = node_weights[min1] + node_weights[min2];
            parents[node_count] = -1;
            parents[min1] = (int16_t)node_count;
            parents[min2] = (int16_t)node_count;
            node_weights[min1] = 0U;
            node_weights[min2] = 0U;
            node_count++;
            active--;
        }

        for (symbol = 0; symbol < symbol_count; ++symbol) {
            int node = symbol;
            unsigned depth = 0U;
            while (parents[node] >= 0) {
                node = parents[node];
                depth++;
            }
            if (depth > BZ2_MAX_CODE_LEN) {
                too_deep = true;
                break;
            }
            candidate[symbol] = (uint8_t)(depth != 0U ? depth : 1U);
        }
        if (!too_deep) {
            xx_rt_memcpy(lengths, candidate, (size_t)symbol_count);
            return true;
        }
        for (symbol = 0; symbol < symbol_count; ++symbol) weights[symbol] = 1U + weights[symbol] / 2U;
    }
}
