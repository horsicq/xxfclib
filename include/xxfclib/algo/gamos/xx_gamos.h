/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_GAMOS_H
#define XXFCLIB_ALGO_GAMOS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Gamos LZSS stream.
 *
 * A 4 KiB ring pre-filled with 0x20 and a write cursor that starts at 0 - not
 * at N - F, which is what the LPAK/AMPK/SZDD variants do.  One LSB-first flag
 * byte per eight tokens, bit set = literal; a match is two bytes b1, b2 with
 * position = ((b2 & 0x0f) << 8) | b1 and length = (b2 >> 4) + 3, i.e. the two
 * nibbles of b2 the other way round from the LPAK variant.
 *
 * The walk is driven by the COMPRESSED size: it ends when the input runs out,
 * and the plaintext length is only checked afterwards.  Every container that
 * uses this codec stores the plaintext length, so @p output_size IS the
 * declared uncompressed size and the decode succeeds only when the stream
 * produces exactly that many bytes.  No scan entry point is needed.
 */
XXFC_API bool xx_gamos_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

#ifdef __cplusplus
}
#endif
#endif
