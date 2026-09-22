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
 * @file xx_zipcrypto.h
 * @brief Traditional PKWARE ZIP encryption decryption helpers.
 */

#ifndef XX_ZIPCRYPTO_H
#define XX_ZIPCRYPTO_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/data/xx_pd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ZIPCRYPTO_HEADER_SIZE 12U
#define XX_ZIPCRYPTO_RANDOM_HEADER_SIZE (XX_ZIPCRYPTO_HEADER_SIZE - 1U)

/**
 * @brief Encrypt a complete traditional ZipCrypto entry envelope.
 *
 * The output begins with the encrypted 12-byte ZipCrypto header and is
 * followed by the encrypted input bytes. The caller supplies the first eleven
 * plaintext header bytes so archive writers can use an appropriate random
 * source while tests can use deterministic vectors. The final header byte is
 * selected from @p crc32 or @p last_mod_time according to
 * @p has_data_descriptor.
 *
 * Passwords are byte strings. An empty password is represented by
 * password_size == 0 and may use a NULL password pointer. Input and output
 * ranges must not overlap.
 *
 * @param input Plain compressed bytes. May be NULL when input_size is zero.
 * @param input_size Number of plain compressed bytes.
 * @param password Password bytes.
 * @param password_size Number of password bytes.
 * @param crc32 Plaintext CRC-32 for verifier selection.
 * @param last_mod_time DOS last-modification time for verifier selection.
 * @param has_data_descriptor General-purpose bit 3 state.
 * @param random_header Eleven caller-supplied plaintext header bytes.
 * @param output Destination for the encrypted header and payload.
 * @param output_capacity Destination size in bytes.
 * @param output_size Receives header plus encrypted payload size on success.
 * @return true when the parameters are valid and the envelope was produced.
 */
XXFC_API bool xx_zipcrypto_encrypt_envelope(
    const uint8_t *input,
    size_t input_size,
    const uint8_t *password,
    size_t password_size,
    uint32_t crc32,
    uint16_t last_mod_time,
    bool has_data_descriptor,
    const uint8_t random_header[XX_ZIPCRYPTO_RANDOM_HEADER_SIZE],
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

/**
 * @brief Decrypt a complete traditional ZipCrypto entry envelope.
 *
 * The input begins with the 12-byte encrypted ZipCrypto header and is followed
 * by the encrypted compressed stream. The header verifier is checked before
 * any payload bytes are written. When @p has_data_descriptor is true, the high
 * byte of @p last_mod_time is the verifier; otherwise the high byte of
 * @p crc32 is used.
 *
 * Passwords are byte strings. This function does not perform text conversion.
 * An empty password is represented by password_size == 0 and may use a NULL
 * password pointer. The exact same address may be used for input and output;
 * other partially overlapping ranges are not supported.
 *
 * Traditional ZipCrypto has only an 8-bit password verifier. A successful
 * return must therefore be followed by decompression and verification of the
 * plaintext CRC-32 by the archive layer.
 *
 * @param envelope Encrypted header and encrypted compressed bytes.
 * @param envelope_size Total envelope size, including the 12-byte header.
 * @param password Password bytes.
 * @param password_size Number of password bytes.
 * @param crc32 Plaintext CRC-32 from the central directory.
 * @param last_mod_time DOS last-modification time from the ZIP header.
 * @param has_data_descriptor General-purpose bit 3 state.
 * @param output Destination for the decrypted compressed stream.
 * @param output_capacity Destination size in bytes.
 * @param output_size Receives the decrypted compressed stream size on success.
 * @return true when parameters and password verifier are valid.
 */
XXFC_API bool xx_zipcrypto_decrypt_envelope(const uint8_t *envelope,
                                            size_t envelope_size,
                                            const uint8_t *password,
                                            size_t password_size,
                                            uint32_t crc32,
                                            uint16_t last_mod_time,
                                            bool has_data_descriptor,
                                            uint8_t *output,
                                            size_t output_capacity,
                                            size_t *output_size);

/** Cancellation-aware envelope variants; the original APIs pass NULL for pd.
 * Password setup and payload loops poll at bounded intervals. Cancellation
 * returns false with output_size zero and securely clears bytes already written
 * by this call. A pre-cancelled call leaves output unchanged. For in-place
 * decryption, cancellation after output starts also alters the envelope; retry
 * from an unchanged source copy. Wrong-password output remains untouched.
 */
XXFC_API bool xx_zipcrypto_encrypt_envelope_progress(
    const uint8_t *input, size_t input_size,
    const uint8_t *password, size_t password_size,
    uint32_t crc32, uint16_t last_mod_time, bool has_data_descriptor,
    const uint8_t random_header[XX_ZIPCRYPTO_RANDOM_HEADER_SIZE],
    uint8_t *output, size_t output_capacity, size_t *output_size,
    xx_pd_struct *pd);
XXFC_API bool xx_zipcrypto_decrypt_envelope_progress(
    const uint8_t *envelope, size_t envelope_size,
    const uint8_t *password, size_t password_size,
    uint32_t crc32, uint16_t last_mod_time, bool has_data_descriptor,
    uint8_t *output, size_t output_capacity, size_t *output_size,
    xx_pd_struct *pd);

/**
 * @brief Return the verifier byte required by a traditional ZIP entry.
 */
XXFC_API uint8_t xx_zipcrypto_verifier_byte(uint32_t crc32,
                                            uint16_t last_mod_time,
                                            bool has_data_descriptor);

#ifdef __cplusplus
}
#endif

#endif /* XX_ZIPCRYPTO_H */
