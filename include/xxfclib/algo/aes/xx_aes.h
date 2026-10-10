/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_aes.h
 * @brief AES primitives and 7-Zip/RAR encryption helpers.
 */

#ifndef XX_AES_H
#define XX_AES_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
/* Preserve source compatibility for callers of the former WinZip API location. */
#include "xxfclib/algo/aes_winzip/xx_aes_winzip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decrypt a 7-Zip AES-256-CBC coder stream.
 *
 * The password must already be encoded as UTF-16LE, without a terminator.
 * Coder properties are the byte sequence stored for method 06 F1 07 01.
 * 7-Zip pads the encrypted stream to an AES block boundary; @p plaintext_size
 * specifies the meaningful prefix of the decrypted result.
 *
 * @return true when the properties and sizes are valid and decryption succeeds.
 * Wrong passwords are detected by the caller through the decoded-stream CRC or
 * parser validation because the 7-Zip AES coder has no password verifier.
 */
XXFC_API bool xx_7zip_aes_decrypt(const uint8_t *input, size_t input_size, const uint8_t *password_utf16le, size_t password_size, const uint8_t *properties,
                                  size_t properties_size, uint8_t *output, size_t output_capacity, size_t plaintext_size);

/** Bounded-memory 7z AES streams. Destinations should be private staging
 * devices: cancellation or failure can leave partial output. Encryption uses
 * fresh OS-random salt/IV and returns the 34-byte coder properties. Decryption
 * must be followed by the archive's CRC validation before publishing output. */
XXFC_API bool xx_7zip_aes_encrypt_device(xx_io_device *source, int64_t source_offset, int64_t plaintext_size, const uint8_t *password_utf16le, size_t password_size,
                                         uint8_t properties[34], size_t *properties_size, xx_io_device *destination, int64_t *output_size, xx_pd_struct *pd);
XXFC_API bool xx_7zip_aes_decrypt_device(xx_io_device *source, int64_t source_offset, int64_t input_size, const uint8_t *password_utf16le, size_t password_size,
                                         const uint8_t *properties, size_t properties_size, int64_t plaintext_size, xx_io_device *destination, xx_pd_struct *pd);

/**
 * Decrypt complete AES-CBC blocks with a 16-, 24-, or 32-byte key.
 * output must hold input_size bytes. Exact in-place operation is supported;
 * other overlapping ranges are not. No padding removal or authentication is
 * performed. The caller must validate the decoded header/file checksum before
 * publishing plaintext. Zero input_size permits NULL input/output pointers.
 */
XXFC_API bool xx_aes_cbc_decrypt(const uint8_t *input, size_t input_size, const uint8_t *key, size_t key_size, const uint8_t iv16[16], uint8_t *output);

/**
 * Derive RAR 3.x/4.x AES-128 key and IV. Password bytes are UTF-16LE without
 * a terminator, truncated to the format's 127 UTF-16-code-unit limit. salt8
 * may be NULL for unsalted entries. Includes historical long-password SHA-1
 * compatibility. Odd password byte counts are rejected. Output buffers must
 * be disjoint from each other and the inputs; they are cleared on failure.
 */
XXFC_API bool xx_rar3_aes_derive(const uint8_t *password_utf16le, size_t password_size, const uint8_t *salt8, uint8_t key16[16], uint8_t iv16[16], xx_pd_struct *pd);

/**
 * Derive RAR5 AES-256 key, checksum key and eight-byte password verifier.
 * Password bytes are UTF-8. kdf_log is a base-2 PBKDF2 iteration exponent;
 * values greater than 24 are rejected. Cancellation is checked throughout.
 * Empty passwords are permitted. Output buffers must be disjoint from each
 * other and the inputs; all outputs are cleared on failure.
 */
XXFC_API bool xx_rar5_aes_derive(const uint8_t *password_utf8, size_t password_size, const uint8_t salt16[16], uint8_t kdf_log, uint8_t key32[32], uint8_t hash_key32[32],
                                 uint8_t check8[8], xx_pd_struct *pd);

/** Constant-time comparison of the stored verifier and its SHA-256 checksum. */
XXFC_API bool xx_rar5_aes_check_password(const uint8_t stored12[12], const uint8_t derived8[8]);

/** RAR5 keyed CRC transform, used when the file encryption MAC flag is set.
 * Native encoders normally leave that flag clear on intermediate split parts.
 * hash_key32 must point to the checksum key from xx_rar5_aes_derive.
 */
XXFC_API uint32_t xx_rar5_aes_mac_crc32(const uint8_t hash_key32[32], uint32_t crc32);

/** RAR5 keyed BLAKE2sp digest transform (HMAC-SHA256 of the 32-byte digest).
 * Apply when the file encryption MAC flag is set. Exact digest32/output32
 * aliasing is supported. Invalid inputs fail and
 * clear output32 when supplied.
 */
XXFC_API bool xx_rar5_aes_mac_hash(const uint8_t hash_key32[32], const uint8_t digest32[32], uint8_t output32[32]);

#ifdef __cplusplus
}
#endif

#endif /* XX_AES_H */
