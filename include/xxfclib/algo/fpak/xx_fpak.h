/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_FPAK_H
#define XXFCLIB_ALGO_FPAK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FoxPro Distribution Kit FPPF payloads are PKZIP Implode streams.  The FPPF
 * header carries the two fields that pick the profile: the compression method
 * (0 = stored, 6 = imploded) and the PKZIP general-purpose flag word.  Only
 * two flag bits reach the codec:
 *
 *   0x02  8 KiB dictionary - seven low distance bits instead of six
 *   0x04  a literal Shannon-Fano tree is present (256 symbols, read BEFORE
 *         the length and distance trees) and the minimum match becomes 3
 *
 * 0x08 (data descriptor) says nothing about the bit stream and is ignored.
 *
 * PASS THE HEADER FIELDS IN, DO NOT GUESS THEM.  A stream decoded with the
 * wrong profile does not fail - it desyncs into plausible garbage.  Check the
 * member CRC-32 afterwards. */
#define XX_FPAK_METHOD_STORED 0
#define XX_FPAK_METHOD_IMPLODED 6
#define XX_FPAK_FLAG_DICTIONARY_8K 0x0002
#define XX_FPAK_FLAG_LITERAL_TREE 0x0004

/**
 * @brief Decode an FPAK member with an explicit FPPF profile.
 *
 * The container always stores the plaintext length, so @p output_size IS the
 * expected uncompressed size: the decode succeeds only when it produces
 * exactly that many bytes AND the bit stream ends exactly at the end of
 * @p input.  No scan entry point is needed or provided.
 */
XXFC_API bool xx_fpak_decode_memory_profile(const uint8_t *input,
                                            size_t input_size,
                                            uint16_t method, uint16_t flags,
                                            uint8_t *output,
                                            size_t output_size,
                                            size_t *written);

/**
 * @brief Decode as much of a member as the bytes in hand allow.
 *
 * A member that spans a media set leaves only its first slice on the lead
 * volume.  That slice still decodes to the true PREFIX of the member - LZ
 * decoding is prefix-correct - and this entry point returns it, reporting the
 * length through @p written.  @p output_size is still the member's full
 * declared plaintext length and still bounds every write.
 *
 * A decode that stops for any reason other than running out of input is a
 * desync and still fails.  The member CRC-32 cannot be checked against a
 * prefix, so a caller must publish the result as an obviously partial member.
 */
XXFC_API bool xx_fpak_decode_partial_profile(const uint8_t *input,
                                             size_t input_size,
                                             uint16_t method, uint16_t flags,
                                             uint8_t *output,
                                             size_t output_size,
                                             size_t *written);

/**
 * @brief Decode an FPAK member using the default profile.
 *
 * Defaults to method 6 with flags 0, which is exactly what the shared
 * decompress chain's decFpakProfile() answers a missing/!=4-byte
 * compress-properties blob with.  Prefer the _profile form whenever the
 * record's four-byte [u16 method][u16 flags] property is available.
 */
XXFC_API bool xx_fpak_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
#endif
