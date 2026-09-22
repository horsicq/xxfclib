/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LIM_H
#define XXFCLIB_ALGO_LIM_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LIM archive method 1 (blocked Huffman + LZ77) decoder.
 *
 * Ported from XArchive/Algos/xlimdecoder.cpp.  The LIM container stores the
 * uncompressed size per member, so no measuring entry point is provided:
 * @p output_size IS the declared plaintext length and the decode succeeds only
 * when exactly that many bytes were produced.
 *
 * Two format details a later reader must not "fix":
 *  - the stream bit is the COMPLEMENT of the Huffman code bit;
 *  - the 32 KiB window carries a parallel per-position match-length history
 *    that is added to every match length.
 *
 * @param input       Compressed bytes of the member.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer, at least @p output_size bytes.
 * @param output_size Declared plaintext length.
 * @param written     Receives the number of bytes produced (0 on failure).
 * @return true only when exactly @p output_size bytes were decoded.
 */
XXFC_API bool xx_lim_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif
