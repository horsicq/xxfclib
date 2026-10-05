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
 * @file xx_aes_winzip.h
 * @brief Authenticated WinZip AES entry-envelope encryption and decryption.
 */
#ifndef XX_AES_WINZIP_H
#define XX_AES_WINZIP_H

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

#ifdef __cplusplus
}
#endif

#endif /* XX_AES_WINZIP_H */
