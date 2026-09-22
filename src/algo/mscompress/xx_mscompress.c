/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native Microsoft Compress LZSS core.  The grammar is an eight-token
 * LSB-first flag group with literal tokens and 12-bit position / 4-bit length
 * matches; it is shared by SZDD (+16) and old SZ (+18) streams.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/mscompress/xx_mscompress.h"

#include <string.h>

#define XX_MSCOMPRESS_WINDOW_SIZE 4096U
#define XX_MSCOMPRESS_MATCH_MINIMUM 3U

bool xx_mscompress_lzss_decode(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               unsigned position_bias, size_t *consumed) {
    uint8_t window[XX_MSCOMPRESS_WINDOW_SIZE];
    size_t input_position = 0U;
    size_t output_position = 0U;
    unsigned flag_bit = 0U;
    uint8_t flags = 0U;
    size_t window_position = 0U;
    if (consumed) *consumed = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        (position_bias != 16U && position_bias != 18U)) {
        return false;
    }
    xx_rt_memset(window, 0x20, sizeof(window));
    while (output_position < output_size) {
        if (flag_bit == 0U) {
            if (input_position == input_size) return false;
            flags = input[input_position++];
        }
        if ((flags & (uint8_t)(UINT8_C(1) << flag_bit)) != 0U) {
            uint8_t value;
            if (input_position == input_size) return false;
            value = input[input_position++];
            output[output_position++] = value;
            window[window_position] = value;
            window_position = (window_position + 1U) &
                              (XX_MSCOMPRESS_WINDOW_SIZE - 1U);
        } else {
            uint8_t low;
            uint8_t high;
            size_t match_position;
            size_t match_length;
            size_t index;
            if (input_size - input_position < 2U) return false;
            low = input[input_position++];
            high = input[input_position++];
            match_position = ((size_t)low |
                              ((size_t)(high & UINT8_C(0xf0)) << 4U));
            match_position = (match_position + position_bias) &
                             (XX_MSCOMPRESS_WINDOW_SIZE - 1U);
            match_length = (size_t)(high & UINT8_C(0x0f)) +
                           XX_MSCOMPRESS_MATCH_MINIMUM;
            if (match_length > output_size - output_position) return false;
            for (index = 0U; index < match_length; ++index) {
                uint8_t value = window[(match_position + index) &
                                       (XX_MSCOMPRESS_WINDOW_SIZE - 1U)];
                output[output_position++] = value;
                window[window_position] = value;
                window_position = (window_position + 1U) &
                                  (XX_MSCOMPRESS_WINDOW_SIZE - 1U);
            }
        }
        flag_bit = (flag_bit + 1U) & 7U;
    }
    if (consumed) *consumed = input_position;
    return true;
}
