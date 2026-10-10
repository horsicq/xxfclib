/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Independent, bounded LArc -lz5- and -lzs- member decoders.
 * Wire format cross-checked against fragglet/lhasa's lz5_decoder.c and
 * lzs_decoder.c; no implementation source was copied.
 */
#ifndef XX_LHA_LEGACY_NATIVE_H
#define XX_LHA_LEGACY_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XX_LHA_LEGACY_LZ5 UINT32_C(0x6c7a35)
#define XX_LHA_LEGACY_LZS UINT32_C(0x6c7a73)

static bool xx_lha_legacy_bit(const uint8_t *input, size_t input_size, size_t *bit_position, unsigned count, unsigned *value)
{
    unsigned result = 0U;
    unsigned i;
    if (!input || !bit_position || !value || count > 16U || input_size > SIZE_MAX / 8U || *bit_position > input_size * 8U || count > input_size * 8U - *bit_position)
        return false;
    for (i = 0U; i < count; ++i) {
        size_t position = (*bit_position)++;
        result = (result << 1U) | ((input[position / 8U] >> (7U - (unsigned)(position & 7U))) & 1U);
    }
    *value = result;
    return true;
}

static bool xx_lha_legacy_decode_native(uint32_t method, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
    uint8_t history[4096];
    size_t source = 0U, destination = 0U, bit_position = 0U;
    unsigned ring_size, ring_position;
    if ((method != XX_LHA_LEGACY_LZ5 && method != XX_LHA_LEGACY_LZS) || !input || !input_size || !output || !output_size || xx_pd_is_stopped(pd)) return false;
    if (method == XX_LHA_LEGACY_LZ5) {
        unsigned i, j;
        size_t index = 0U;
        ring_size = 4096U;
        ring_position = 4096U - 18U;
        for (i = 0U; i < 256U; ++i)
            for (j = 0U; j < 13U; ++j) history[index++] = (uint8_t)i;
        for (i = 0U; i < 256U; ++i) history[index++] = (uint8_t)i;
        for (i = 0U; i < 256U; ++i) history[index++] = (uint8_t)(255U - i);
        memset(history + index, 0, 128U);
        index += 128U;
        memset(history + index, ' ', 110U);
        index += 110U;
        memset(history + index, 0, 18U);
    } else {
        ring_size = 2048U;
        ring_position = 2048U - 17U;
        memset(history, ' ', ring_size);
    }
    while (destination < output_size) {
        unsigned flags = 0U, command;
        if (xx_pd_is_stopped(pd)) return false;
        if (method == XX_LHA_LEGACY_LZ5) {
            if (source >= input_size) return false;
            flags = input[source++];
        }
        for (command = 0U; command < (method == XX_LHA_LEGACY_LZ5 ? 8U : 1U) && destination < output_size; ++command) {
            unsigned literal, start, length, i;
            if ((destination & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
            if (method == XX_LHA_LEGACY_LZ5) {
                literal = (flags >> command) & 1U;
                if (literal) {
                    if (source >= input_size) return false;
                    output[destination] = input[source++];
                    history[ring_position] = output[destination++];
                    ring_position = (ring_position + 1U) & (ring_size - 1U);
                    continue;
                }
                if (input_size - source < 2U) return false;
                start = (unsigned)input[source] | (((unsigned)input[source + 1U] & 0xf0U) << 4U);
                length = ((unsigned)input[source + 1U] & 0x0fU) + 3U;
                source += 2U;
            } else {
                if (!xx_lha_legacy_bit(input, input_size, &bit_position, 1U, &literal)) return false;
                if (literal) {
                    if (!xx_lha_legacy_bit(input, input_size, &bit_position, 8U, &i)) return false;
                    output[destination] = (uint8_t)i;
                    history[ring_position] = output[destination++];
                    ring_position = (ring_position + 1U) & (ring_size - 1U);
                    continue;
                }
                if (!xx_lha_legacy_bit(input, input_size, &bit_position, 11U, &start) || !xx_lha_legacy_bit(input, input_size, &bit_position, 4U, &length)) return false;
                length += 2U;
            }
            if (length > output_size - destination) return false;
            for (i = 0U; i < length; ++i) {
                uint8_t byte = history[(start + i) & (ring_size - 1U)];
                output[destination++] = byte;
                history[ring_position] = byte;
                ring_position = (ring_position + 1U) & (ring_size - 1U);
            }
        }
    }
    if (method == XX_LHA_LEGACY_LZ5) {
        /* GEMDOS/LArc writers in the SFX corpus count one zero alignment
         * byte in the packed size after the final flag group.  Four clean
         * reference-validated fixtures decode byte-for-byte and stop at precisely this
         * byte.  Keep every other trailing byte or longer tail invalid. */
        if (source != input_size && !(input_size - source == 1U && input[source] == 0U)) return false;
    } else if ((bit_position + 7U) / 8U != input_size) return false;
    return !xx_pd_is_stopped(pd);
}
#endif
