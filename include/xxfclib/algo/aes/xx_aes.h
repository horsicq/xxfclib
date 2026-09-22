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
 * @brief Authenticated WinZip AES envelope decryption.
 */

#ifndef XX_AES_H
#define XX_AES_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/data/xx_pd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE 2U
#define XX_WINZIP_AES_AUTH_CODE_SIZE         10U

typedef enum xx_winzip_aes_strength_e {
    XX_WINZIP_AES_STRENGTH_128 = 1,
    XX_WINZIP_AES_STRENGTH_192 = 2,
    XX_WINZIP_AES_STRENGTH_256 = 3
} xx_winzip_aes_strength;

/**
 * @brief Return the salt length for a WinZip AES strength, or zero if invalid.
 */
XXFC_API size_t xx_winzip_aes_salt_size(uint8_t strength);

/**
 * @brief Return the AES key length for a WinZip AES strength, or zero if invalid.
 */
XXFC_API size_t xx_winzip_aes_key_size(uint8_t strength);

/**
 * @brief Encrypt and authenticate a complete WinZip AES entry envelope.
 *
 * The output layout is salt, two-byte password verifier, encrypted compressed
 * data, and the ten-byte authentication code. The caller supplies the salt so
 * archive writers can use an appropriate random source while tests can use
 * deterministic vectors. The salt length must exactly match the strength.
 *
 * Passwords are byte strings. An empty password is represented by
 * password_size == 0 and may use a NULL password pointer. A zero-length input
 * is valid. Input and output ranges must not overlap.
 *
 * @param input Plain compressed bytes. May be NULL when input_size is zero.
 * @param input_size Number of plain compressed bytes.
 * @param password Password bytes.
 * @param password_size Number of password bytes.
 * @param strength AES strength value (1, 2, or 3).
 * @param salt Caller-supplied salt bytes.
 * @param salt_size Number of salt bytes; must match @p strength.
 * @param output Destination for the complete encrypted envelope.
 * @param output_capacity Destination size in bytes.
 * @param output_size Receives the complete envelope size on success.
 * @return true when the parameters are valid and the envelope was produced.
 */
XXFC_API bool xx_winzip_aes_encrypt_envelope(const uint8_t *input,
                                             size_t input_size,
                                             const uint8_t *password,
                                             size_t password_size,
                                             uint8_t strength,
                                             const uint8_t *salt,
                                             size_t salt_size,
                                             uint8_t *output,
                                             size_t output_capacity,
                                             size_t *output_size);

/**
 * @brief Decrypt and authenticate a complete WinZip AES entry envelope.
 *
 * The input layout is salt, two-byte password verifier, encrypted compressed
 * data, and the ten-byte authentication code. PBKDF2-HMAC-SHA1 with 1000
 * iterations derives the encryption key, authentication key, and verifier.
 * The HMAC is checked in constant time before decrypted bytes are published.
 *
 * Passwords are byte strings; no text conversion is performed. An empty
 * password is represented by password_size == 0 and may use a NULL password
 * pointer. A zero-length encrypted payload is valid. The exact same address may
 * be used for input and output; other partially overlapping ranges are not
 * supported.
 *
 * @param envelope Full WinZip AES entry data envelope.
 * @param envelope_size Total envelope size.
 * @param password Password bytes.
 * @param password_size Number of password bytes.
 * @param strength AES strength value from extra field 0x9901 (1, 2, or 3).
 * @param output Destination for decrypted compressed bytes.
 * @param output_capacity Destination size in bytes.
 * @param output_size Receives the decrypted compressed byte count on success.
 * @return true only if the password verifier and authentication code are valid.
 */
XXFC_API bool xx_winzip_aes_decrypt_envelope(const uint8_t *envelope,
                                             size_t envelope_size,
                                             const uint8_t *password,
                                             size_t password_size,
                                             uint8_t strength,
                                             uint8_t *output,
                                             size_t output_capacity,
                                             size_t *output_size);

/** Cancellation-aware variants of the WinZip AES envelope functions.
 * The original APIs are equivalent to passing NULL for pd. Password derivation,
 * authentication and payload processing poll cancellation at bounded intervals.
 * On failure output_size is zero; any bytes written by this call are securely
 * cleared. Cancellation before output begins leaves the destination unchanged.
 * An interrupted in-place decryption can therefore overwrite input bytes and
 * must be retried from the original envelope, not the partially cleared buffer.
 */
XXFC_API bool xx_winzip_aes_encrypt_envelope_progress(
    const uint8_t *input, size_t input_size,
    const uint8_t *password, size_t password_size, uint8_t strength,
    const uint8_t *salt, size_t salt_size,
    uint8_t *output, size_t output_capacity, size_t *output_size,
    xx_pd_struct *pd);
XXFC_API bool xx_winzip_aes_decrypt_envelope_progress(
    const uint8_t *envelope, size_t envelope_size,
    const uint8_t *password, size_t password_size, uint8_t strength,
    uint8_t *output, size_t output_capacity, size_t *output_size,
    xx_pd_struct *pd);

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
XXFC_API bool xx_7zip_aes_decrypt(const uint8_t *input,
                                  size_t input_size,
                                  const uint8_t *password_utf16le,
                                  size_t password_size,
                                  const uint8_t *properties,
                                  size_t properties_size,
                                  uint8_t *output,
                                  size_t output_capacity,
                                  size_t plaintext_size);

/**
 * Decrypt complete AES-CBC blocks with a 16-, 24-, or 32-byte key.
 * output must hold input_size bytes. Exact in-place operation is supported;
 * other overlapping ranges are not. No padding removal or authentication is
 * performed. The caller must validate the decoded header/file checksum before
 * publishing plaintext. Zero input_size permits NULL input/output pointers.
 */
XXFC_API bool xx_aes_cbc_decrypt(const uint8_t *input, size_t input_size,
                                 const uint8_t *key, size_t key_size,
                                 const uint8_t iv16[16], uint8_t *output);

/**
 * Derive RAR 3.x/4.x AES-128 key and IV. Password bytes are UTF-16LE without
 * a terminator, truncated to the format's 127 UTF-16-code-unit limit. salt8
 * may be NULL for unsalted entries. Includes historical long-password SHA-1
 * compatibility. Odd password byte counts are rejected. Output buffers must
 * be disjoint from each other and the inputs; they are cleared on failure.
 */
XXFC_API bool xx_rar3_aes_derive(const uint8_t *password_utf16le,
                                 size_t password_size, const uint8_t *salt8,
                                 uint8_t key16[16], uint8_t iv16[16],
                                 xx_pd_struct *pd);

/**
 * Derive RAR5 AES-256 key, checksum key and eight-byte password verifier.
 * Password bytes are UTF-8. kdf_log is a base-2 PBKDF2 iteration exponent;
 * values greater than 24 are rejected. Cancellation is checked throughout.
 * Empty passwords are permitted. Output buffers must be disjoint from each
 * other and the inputs; all outputs are cleared on failure.
 */
XXFC_API bool xx_rar5_aes_derive(const uint8_t *password_utf8,
                                 size_t password_size, const uint8_t salt16[16],
                                 uint8_t kdf_log, uint8_t key32[32],
                                 uint8_t hash_key32[32], uint8_t check8[8],
                                 xx_pd_struct *pd);

/** Constant-time comparison of the stored verifier and its SHA-256 checksum. */
XXFC_API bool xx_rar5_aes_check_password(const uint8_t stored12[12],
                                         const uint8_t derived8[8]);

/** RAR5 keyed CRC transform, used when the file encryption MAC flag is set.
 * Native encoders normally leave that flag clear on intermediate split parts.
 * hash_key32 must point to the checksum key from xx_rar5_aes_derive.
 */
XXFC_API uint32_t xx_rar5_aes_mac_crc32(const uint8_t hash_key32[32],
                                       uint32_t crc32);

/** RAR5 keyed BLAKE2sp digest transform (HMAC-SHA256 of the 32-byte digest).
 * Apply when the file encryption MAC flag is set. Exact digest32/output32
 * aliasing is supported. Invalid inputs fail and
 * clear output32 when supplied.
 */
XXFC_API bool xx_rar5_aes_mac_hash(const uint8_t hash_key32[32],
                                   const uint8_t digest32[32], uint8_t output32[32]);

#ifdef __cplusplus
}
#endif

#endif /* XX_AES_H */
