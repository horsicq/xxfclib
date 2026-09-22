/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gamos LZSS.  Ported from the HANDLE_METHOD_GAMOS arm of
 * XArchive/core/xdecompress.cpp; the token walk, the flag-word trick and the
 * two end-of-stream behaviours below are that code's, not a reconstruction.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/gamos/xx_gamos.h"

#define GAMOS_WINDOW_SIZE 4096
#define GAMOS_WINDOW_MASK 0xfff

bool xx_gamos_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written)
{
    uint8_t window[GAMOS_WINDOW_SIZE];
    size_t position = 0U;
    size_t left;
    size_t produced = 0U;
    uint32_t flags = 0U;
    int32_t ring = 0;
    bool ok = true;

    if (written) *written = 0U;
    if ((!input && input_size) || (!output && output_size)) return false;

    /* The ring is pre-filled with spaces, and the FIRST write goes to index 0
     * rather than to N - F.  Both are deliberate: a match that reaches back
     * before anything was produced legally yields 0x20 bytes here. */
    xx_rt_memset(window, 0x20, sizeof(window));

    left = input_size;
    while (left > 0U) {
        uint32_t byte = input[position++];
        --left;
        /* The shift happens BEFORE the exhaustion test, so bit 8 of the
         * sentinel 0xff00 walks down and clears exactly after eight tokens. */
        flags >>= 1;
        if ((flags & 0x100U) == 0U) {
            flags = byte | 0xff00U;
            if (left < 1U) {
                ok = false;
                break;
            }
            byte = input[position++];
            --left;
        }
        if (flags & 1U) {
            window[ring] = (uint8_t)byte;
            if (produced >= output_size) {
                ok = false;
                break;
            }
            output[produced++] = (uint8_t)byte;
            ring = (ring + 1) & GAMOS_WINDOW_MASK;
        } else {
            uint32_t second;
            int32_t source;
            int32_t length;
            int32_t i;
            /* Running out of input in front of a match is a clean end of
             * stream in the original, not an error.  Do not "fix" this: the
             * declared-length check below is what decides the outcome. */
            if (left < 1U) break;
            second = input[position++];
            --left;
            source = (int32_t)(((second & 0x0fU) << 8) | byte);
            length = (int32_t)(second >> 4) + 3;
            if ((size_t)length > output_size - produced) {
                ok = false;
                break;
            }
            for (i = 0; i < length; ++i) {
                const uint8_t copied = window[source & GAMOS_WINDOW_MASK];
                window[ring] = copied;
                source = (source & GAMOS_WINDOW_MASK) + 1;
                output[produced++] = copied;
                ring = (ring + 1) & GAMOS_WINDOW_MASK;
            }
        }
    }

    if (!ok || (produced != output_size)) return false;

    if (written) *written = produced;
    return true;
}
