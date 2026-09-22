/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_PANORAMA_H
#define XXFCLIB_ALGO_PANORAMA_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panorama - a whole-file XOR against a 1024-byte pad generated from one
 * 32-bit seed by the Borland LCG multiplier 0x8088405.  The plaintext under
 * the cipher is an ordinary RAR 4.x archive, and that known plaintext is what
 * yields the seed:
 *
 *     seed = u32le(file[0:4]) ^ 0x21726152                  ("Rar!")
 *     (u32le(file[4:8]) ^ (seed * 0x8088405)) & 0xffffff == 0x71a
 *
 * Pad: k[0] = seed, k[n+1] = k[n] * 0x8088405 (mod 2^32), the 256 words
 * stored little-endian; plain[i] = cipher[i] ^ pad[i & 0x3ff], where i is the
 * ABSOLUTE FILE OFFSET.  The transform is therefore its own inverse and the
 * decoded size always equals the input size.
 *
 * The reference (XArchive/Algos/xpanoramadecoder.cpp) searches an 18-entry
 * table of multipliers to arrive at the same seed; that table is pure
 * obfuscation of the single test above and the reference itself does not port
 * it.  Do not "restore" it.
 */

/**
 * @brief Recover the stream seed from the first eight bytes of the file.
 *
 * Matches XPanoramaDecoder::seedFromHeader, including its rejection of a zero
 * seed (a zero seed means the file literally begins with "Rar!", i.e. it is
 * not enciphered at all).
 */
XXFC_API bool xx_panorama_seed_from_header(const uint8_t *header,
                                           size_t header_size,
                                           uint32_t *seed);

/**
 * @brief Decipher a whole Panorama file, deriving the seed from its header.
 *
 * @p input must be the WHOLE FILE from offset 0 - the pad phase is tied to
 * offset 0, so a partial view decodes to garbage.  @p output_size must be at
 * least @p input_size; exactly @p input_size bytes are produced.
 */
XXFC_API bool xx_panorama_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

/**
 * @brief Same, with the seed supplied (the container's compress property).
 *
 * This is the exact analogue of XPanoramaDecoder::decode: it performs NO
 * signature check, because the caller already validated the seed when it
 * parsed the container.
 */
XXFC_API bool xx_panorama_decode_memory_seed(const uint8_t *input,
                                             size_t input_size, uint32_t seed,
                                             uint8_t *output,
                                             size_t output_size,
                                             size_t *written);

/**
 * @brief Measure a Panorama file.
 *
 * The cipher is length-preserving, so this only validates the header and
 * reports @p produced == @p consumed == @p input_size.  Provided so a reader
 * has one call that both sizes an allocation and rejects a non-Panorama file.
 */
XXFC_API bool xx_panorama_scan_memory(const uint8_t *input, size_t input_size,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_PANORAMA_H */
