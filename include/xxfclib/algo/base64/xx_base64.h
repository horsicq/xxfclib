/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_base64.h
 * @brief Buffer base64 coding with a caller-supplied 64-character alphabet.
 *
 * Encoding emits either the canonical unpadded form (every full 3-byte group
 * becomes 4 characters, a 1- or 2-byte tail becomes 2 or 3) or pads the tail
 * with '=' to a multiple of four characters. Decoding is strict: it accepts
 * the unpadded form and tails of 2 or 3 characters, rejects characters
 * outside the alphabet, non-canonical tail bits and a total length of 1 mod 4.
 */

#ifndef XX_BASE64_H
#define XX_BASE64_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exact number of characters xx_base64_encode_buffer writes for
 * length input bytes. Returns 0 for empty input and for lengths whose
 * encoded size is not representable (indistinguishable; encode fails its
 * own overflow checks regardless).
 */
XXFC_API size_t xx_base64_encoded_size(size_t length, bool pad);

/**
 * @brief Encode a buffer into out (no NUL terminator).
 *
 * @param in Input bytes; may be NULL only when length is 0.
 * @param alphabet Exactly 64 coding characters, in symbol order.
 * @param pad Append '=' so the output is a multiple of 4 characters.
 * @param out Output buffer, must hold xx_base64_encoded_size(length, pad) bytes.
 * @param out_size Size of out in bytes.
 * @param out_length Optional receiving slot for the number of characters written.
 * @return false on NULL arguments, an unrepresentable length, or an undersized out.
 */
XXFC_API bool xx_base64_encode_buffer(const uint8_t *in, size_t length,
                                      const char alphabet[64], bool pad,
                                      char *out, size_t out_size,
                                      size_t *out_length);

/**
 * @brief Strictly decode an unpadded base64 buffer.
 *
 * A trailing group of 2 characters yields 1 byte, of 3 characters 2 bytes;
 * both must have zero-valued discarded bits, as must every group of 4.
 * Characters outside alphabet are rejected, so padded input must be stripped
 * by the caller first.
 *
 * @param in Encoded characters.
 * @param alphabet The same 64 coding characters used for encoding.
 * @param out Output buffer, must hold the decoded size of in.
 * @param out_capacity Size of out in bytes.
 * @param out_length Optional receiving slot for the number of bytes written.
 * @return false on NULL arguments, invalid characters, invalid tail bits or a
 *         length of 1 mod 4.
 */
XXFC_API bool xx_base64_decode_buffer(const char *in, size_t length,
                                      const char alphabet[64],
                                      uint8_t *out, size_t out_capacity,
                                      size_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* XX_BASE64_H */
