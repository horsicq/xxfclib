/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Coktel Vision STK/ITK LZSS.  Ported from XArchive/Algos/xcoktellzdecoder.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/coktellz/xx_coktellz.h"

#define COKTEL_WINDOW_SIZE 4096U
#define COKTEL_WINDOW_MASK 4095U
#define COKTEL_START_POS 4078U /* N - F, the classic Okumura start position */
#define COKTEL_MATCH_MIN_LENGTH 3U

XXFC_API bool xx_coktellz_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written)
{
    uint8_t window[COKTEL_WINDOW_SIZE];
    size_t offset = 0U;
    size_t total = 0U;
    uint32_t window_pos = COKTEL_START_POS;
    uint8_t flag_byte = 0U;
    unsigned flag_bit_pos = 0U;

    if (written) *written = 0U;
    if (!input || (!output && (output_size != 0U))) return false;

    /* The window starts filled with spaces; matches may read those before
     * anything has been written, so this is part of the format. */
    xx_rt_memset(window, 0x20, sizeof(window));

    while (total < output_size) {
        bool is_literal;

        if (flag_bit_pos == 0U) {
            /* A sized member must produce exactly its declared size, so a
             * physical EOF here is an error, not a clean end. */
            if (offset >= input_size) return false;
            flag_byte = input[offset];
            offset++;
        }

        is_literal = (flag_byte & (uint8_t)(1U << flag_bit_pos)) != 0U;
        flag_bit_pos = (flag_bit_pos + 1U) & 7U;

        if (is_literal) {
            uint8_t byte;
            if (offset >= input_size) return false;
            byte = input[offset];
            offset++;
            output[total] = byte;
            total++;
            window[window_pos] = byte;
            window_pos = (window_pos + 1U) & COKTEL_WINDOW_MASK;
        } else {
            uint8_t first_byte;
            uint8_t second_byte;
            unsigned length;
            uint32_t position;
            unsigned i;

            if ((input_size < 2U) || (offset > input_size - 2U)) return false;
            first_byte = input[offset];
            second_byte = input[offset + 1U];
            offset += 2U;

            length = (unsigned)(second_byte & 0x0fU) + COKTEL_MATCH_MIN_LENGTH;
            /* No offset bias, unlike the MS SZDD variant (Coktel). */
            position = (uint32_t)((((uint32_t)(second_byte & 0xf0U) << 4) |
                                   (uint32_t)first_byte) &
                                  COKTEL_WINDOW_MASK);

            for (i = 0U; i < length; ++i) {
                uint8_t byte;
                /* Deliberate, and matches the reference: the last match may
                 * cross the declared uncompressed size; copy only up to it
                 * rather than treating the overshoot as an error. */
                if (total >= output_size) break;
                byte = window[(position + i) & COKTEL_WINDOW_MASK];
                output[total] = byte;
                total++;
                window[window_pos] = byte;
                window_pos = (window_pos + 1U) & COKTEL_WINDOW_MASK;
            }
        }
    }

    if (total != output_size) return false;

    if (written) *written = total;

    return true;
}
