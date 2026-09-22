/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_KOLIBRIKPACK_H
#define XXFCLIB_ALGO_KOLIBRIKPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a KolibriOS kpack ("KPCK") container.
 *
 * @p input must start at the 'KPCK' magic and cover the WHOLE container: the
 * LZMA stream begins at +12 and the optional x86 call-trick filter reads a
 * five-byte trailer from the very end of the buffer.
 *
 * The payload is LZMA1 with hard-wired lc=3 lp=0 pb=2, no 13-byte LZMA header,
 * and a range coder initialised from the first four payload bytes read LITTLE-
 * endian -- kpack byte-swaps that dword before handing it to a stock five-byte
 * initialiser, so no dummy byte is skipped.
 *
 * @param input       Whole container, starting at the magic.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity; must be at least the header's unpacked size.
 * @param written     Receives the number of bytes produced (0 on failure).
 * @return true only when the full declared plaintext was produced.
 */
XXFC_API bool xx_kolibrikpack_decode_memory(const uint8_t *input, size_t input_size,
                                            uint8_t *output, size_t output_size,
                                            size_t *written);

/**
 * @brief Read the unpacked size out of a kpack header without decoding.
 *
 * The container stores its plaintext length, so this is a header check rather
 * than a trial decode.  Applies the same acceptance rules as the reference:
 * LZMA plus at most one of the two call-trick filters, non-zero size, and a
 * container long enough to hold the stream and any trailer.
 *
 * @param input       Whole container, starting at the magic.
 * @param input_size  Length of @p input.
 * @param produced    Receives the unpacked size. May be NULL.
 * @return true when the header is a valid kpack header.
 */
XXFC_API bool xx_kolibrikpack_check_header(const uint8_t *input, size_t input_size,
                                           size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
