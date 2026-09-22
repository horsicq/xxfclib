/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_SQZ_H
#define XXFCLIB_ALGO_SQZ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Squeeze It (HLSQZ) methods 1..4.
 *
 * All four methods share one bitstream: 14-bit block token counts, an LHA-ish
 * pre-tree / literal-length tree / distance tree triple, and a 32 KiB window.
 * Only two small decisions differ, which is why this is one core routine with
 * a method selector rather than four decoders:
 *
 *   - length map   : methods 1 and 2 use the "compact" form (C symbols below
 *                    0x1c0 are lengths directly, 0x1c0.. carry one extra bit);
 *                    methods 3 and 4 use the extended base/extra table over
 *                    C symbols 0x100..0x11f.
 *   - distance map : methods 1 and 3 use the compact power-of-two map
 *                    (symbol s >= 2 -> (1 << (s-1)) + extra(s-1));
 *                    methods 2 and 4 use the 31-entry base/extra table.
 *
 * The XSQZ container stores each member's uncompressed size in its header, so
 * no measuring entry point is provided: @p output_size IS the declared
 * plaintext length and the decode must land on it exactly.
 */

/**
 * @brief Decode one SQZ member body.
 *
 * @param input       The member's compressed bytes, exactly.
 * @param input_size  Length of @p input. The whole extent must be consumed
 *                    (the format tolerates at most two slack bytes).
 * @param method      1, 2, 3 or 4.
 * @param output      Destination buffer.
 * @param output_size The member's declared uncompressed size. The stream must
 *                    produce exactly this many bytes.
 * @param written     Receives the byte count produced; 0 on every failure.
 * @return true only on a complete, exact decode.
 */
XXFC_API bool xx_sqz_decode_memory(const uint8_t *input, size_t input_size,
                                   uint32_t method, uint8_t *output,
                                   size_t output_size, size_t *written);

/* Per-method convenience wrappers with the library's standard shape. */
XXFC_API bool xx_sqz1_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);
XXFC_API bool xx_sqz2_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);
XXFC_API bool xx_sqz3_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);
XXFC_API bool xx_sqz4_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
#endif
