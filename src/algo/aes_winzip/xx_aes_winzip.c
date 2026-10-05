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

#include "xxfclib/algo/aes_winzip/xx_aes_winzip.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../aes/xx_aes_internal.h"

#define XX_WINZIP_AES_MAX_DERIVED   66U
#define XX_WINZIP_AES_PBKDF2_ROUNDS 1000U

static void xx_crypto_clear(void *data, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static void xx_bytes_zero(uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        data[index] = 0U;
    }
}

static void xx_bytes_copy(uint8_t *destination, const uint8_t *source, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        destination[index] = source[index];
    }
}

static void xx_store_be32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static bool xx_sha1_update_progress(xx_sha1_context *context,
                                      const uint8_t *data, size_t data_size,
                                      xx_pd_struct *pd) {
    size_t done = 0;
    while (done < data_size) {
        size_t amount = data_size - done;
        if (xx_pd_is_stopped(pd)) return false;
        if (amount > 4096U) amount = 4096U;
        xx_sha1_update(context, data + done, amount);
        done += amount;
    }
    return !xx_pd_is_stopped(pd);
}

static bool xx_hmac_sha1_parts(const uint8_t *key, size_t key_size,
                               const uint8_t *part1, size_t part1_size,
                               const uint8_t *part2, size_t part2_size,
                               uint8_t digest[XX_SHA1_DIGEST_SIZE],
                               xx_pd_struct *pd) {
    uint8_t key_block[XX_SHA1_BLOCK_SIZE];
    uint8_t inner_pad[XX_SHA1_BLOCK_SIZE];
    uint8_t outer_pad[XX_SHA1_BLOCK_SIZE];
    uint8_t inner_digest[XX_SHA1_DIGEST_SIZE];
    xx_sha1_context context;
    size_t index;
    bool success = false;

    xx_bytes_zero(key_block, sizeof(key_block));
    if (xx_pd_is_stopped(pd)) goto cleanup;
    if (key_size > XX_SHA1_BLOCK_SIZE) {
        xx_sha1_init(&context);
        if (!xx_sha1_update_progress(&context, key, key_size, pd)) goto cleanup;
        xx_sha1_final(&context, key_block, XX_SHA1_DIGEST_SIZE);
    } else if (key_size > 0U) {
        xx_bytes_copy(key_block, key, key_size);
    }

    for (index = 0U; index < XX_SHA1_BLOCK_SIZE; ++index) {
        inner_pad[index] = (uint8_t)(key_block[index] ^ 0x36U);
        outer_pad[index] = (uint8_t)(key_block[index] ^ 0x5CU);
    }

    xx_sha1_init(&context);
    xx_sha1_update(&context, inner_pad, sizeof(inner_pad));
    if (!xx_sha1_update_progress(&context, part1, part1_size, pd) ||
        !xx_sha1_update_progress(&context, part2, part2_size, pd)) goto cleanup;
    xx_sha1_final(&context, inner_digest, XX_SHA1_DIGEST_SIZE);

    xx_sha1_init(&context);
    xx_sha1_update(&context, outer_pad, sizeof(outer_pad));
    xx_sha1_update(&context, inner_digest, sizeof(inner_digest));
    xx_sha1_final(&context, digest, XX_SHA1_DIGEST_SIZE);
    success = !xx_pd_is_stopped(pd);

cleanup:
    if (!success) xx_crypto_clear(digest, XX_SHA1_DIGEST_SIZE);
    xx_crypto_clear(&context, sizeof(context));
    xx_crypto_clear(key_block, sizeof(key_block));
    xx_crypto_clear(inner_pad, sizeof(inner_pad));
    xx_crypto_clear(outer_pad, sizeof(outer_pad));
    xx_crypto_clear(inner_digest, sizeof(inner_digest));
    return success;
}

static bool xx_pbkdf2_hmac_sha1(const uint8_t *password, size_t password_size,
                                const uint8_t *salt, size_t salt_size,
                                uint8_t *derived, size_t derived_size,
                                xx_pd_struct *pd) {
    uint8_t current[XX_SHA1_DIGEST_SIZE];
    uint8_t accumulated[XX_SHA1_DIGEST_SIZE];
    uint8_t block_index[4];
    size_t produced = 0U;
    uint32_t block_number = 1U;
    bool success = false;

    while (produced < derived_size) {
        unsigned int iteration;
        size_t index;
        size_t amount;

        xx_store_be32(block_index, block_number);
        if (!xx_hmac_sha1_parts(password, password_size, salt, salt_size,
                           block_index, sizeof(block_index), current, pd)) goto cleanup;
        xx_bytes_copy(accumulated, current, sizeof(accumulated));

        for (iteration = 1U; iteration < XX_WINZIP_AES_PBKDF2_ROUNDS; ++iteration) {
            if (!xx_hmac_sha1_parts(password, password_size, current, sizeof(current),
                               NULL, 0U, current, pd)) goto cleanup;
            for (index = 0U; index < sizeof(accumulated); ++index) {
                accumulated[index] ^= current[index];
            }
        }

        amount = derived_size - produced;
        if (amount > XX_SHA1_DIGEST_SIZE) {
            amount = XX_SHA1_DIGEST_SIZE;
        }
        xx_bytes_copy(derived + produced, accumulated, amount);
        produced += amount;
        ++block_number;
    }
    success = !xx_pd_is_stopped(pd);

cleanup:
    if (!success) xx_crypto_clear(derived, derived_size);
    xx_crypto_clear(current, sizeof(current));
    xx_crypto_clear(accumulated, sizeof(accumulated));
    xx_crypto_clear(block_index, sizeof(block_index));
    return success;
}

static bool xx_winzip_aes_ctr_crypt(const xx_aes_context *context,
                                    const uint8_t *input, uint8_t *output,
                                    size_t size, size_t *written,
                                    xx_pd_struct *pd) {
    uint8_t counter[XX_AES_BLOCK_SIZE];
    uint8_t key_stream[XX_AES_BLOCK_SIZE];
    size_t offset = 0U;
    bool success = false;

    *written = 0;

    xx_bytes_zero(counter, sizeof(counter));
    counter[0] = 1U;

    while (offset < size) {
        size_t block_size = size - offset;
        size_t index;
        unsigned int counter_byte;

        if ((offset & 4095U) == 0 && xx_pd_is_stopped(pd)) goto cleanup;

        if (block_size > XX_AES_BLOCK_SIZE) {
            block_size = XX_AES_BLOCK_SIZE;
        }
        xx_aes_internal_encrypt_block(context, counter, key_stream);
        for (index = 0U; index < block_size; ++index) {
            output[offset + index] = (uint8_t)(input[offset + index] ^ key_stream[index]);
        }

        for (counter_byte = 0U; counter_byte < 8U; ++counter_byte) {
            ++counter[counter_byte];
            if (counter[counter_byte] != 0U) {
                break;
            }
        }
        offset += block_size;
    }
    success = !xx_pd_is_stopped(pd);

cleanup:
    *written = offset;
    xx_crypto_clear(counter, sizeof(counter));
    xx_crypto_clear(key_stream, sizeof(key_stream));
    return success;
}

static bool xx_constant_time_equal(const uint8_t *left, const uint8_t *right,
                                   size_t size) {
    uint8_t difference = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        difference |= (uint8_t)(left[index] ^ right[index]);
    }
    return difference == 0U;
}

size_t xx_winzip_aes_salt_size(uint8_t strength) {
    switch (strength) {
        case XX_WINZIP_AES_STRENGTH_128: return 8U;
        case XX_WINZIP_AES_STRENGTH_192: return 12U;
        case XX_WINZIP_AES_STRENGTH_256: return 16U;
        default: return 0U;
    }
}

size_t xx_winzip_aes_key_size(uint8_t strength) {
    switch (strength) {
        case XX_WINZIP_AES_STRENGTH_128: return 16U;
        case XX_WINZIP_AES_STRENGTH_192: return 24U;
        case XX_WINZIP_AES_STRENGTH_256: return 32U;
        default: return 0U;
    }
}

bool xx_winzip_aes_encrypt_envelope(const uint8_t *input,
                                    size_t input_size,
                                    const uint8_t *password,
                                    size_t password_size,
                                    uint8_t strength,
                                    const uint8_t *salt,
                                    size_t salt_size,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_size) {
    return xx_winzip_aes_encrypt_envelope_progress(input,input_size,
        password,password_size,strength,salt,salt_size,
        output,output_capacity,output_size,NULL);
}

bool xx_winzip_aes_encrypt_envelope_progress(const uint8_t *input,
                                    size_t input_size,
                                    const uint8_t *password,
                                    size_t password_size,
                                    uint8_t strength,
                                    const uint8_t *salt,
                                    size_t salt_size,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_size,
                                    xx_pd_struct *pd) {
    uint8_t derived[XX_WINZIP_AES_MAX_DERIVED];
    uint8_t computed_auth[XX_SHA1_DIGEST_SIZE];
    xx_aes_context aes_context;
    size_t expected_salt_size = xx_winzip_aes_salt_size(strength);
    size_t key_size = xx_winzip_aes_key_size(strength);
    size_t derived_size;
    size_t overhead;
    size_t envelope_size;
    uint8_t *encrypted_data;
    size_t written = 0;
    size_t payload_written = 0;
    bool success = false;

    if (output_size) {
        *output_size = 0U;
    }
    xx_bytes_zero(derived, sizeof(derived));
    xx_bytes_zero(computed_auth, sizeof(computed_auth));
    xx_bytes_zero((uint8_t *)&aes_context, sizeof(aes_context));
    if (xx_pd_is_stopped(pd)) goto cleanup;

    overhead = expected_salt_size + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE +
               XX_WINZIP_AES_AUTH_CODE_SIZE;
    if (expected_salt_size == 0U || key_size == 0U ||
        salt_size != expected_salt_size || !salt || !output ||
        (input_size > 0U && !input) ||
        (password_size > 0U && !password) ||
        input_size > SIZE_MAX - overhead) {
        goto cleanup;
    }
    envelope_size = overhead + input_size;
    if (envelope_size > output_capacity) {
        goto cleanup;
    }

    derived_size = (key_size * 2U) + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE;
    if (derived_size > sizeof(derived) ||
        !xx_pbkdf2_hmac_sha1(password, password_size, salt, salt_size,
                             derived, derived_size, pd) ||
        !xx_aes_internal_set_key(&aes_context, derived, key_size)) {
        goto cleanup;
    }

    xx_bytes_copy(output, salt, salt_size);
    xx_bytes_copy(output + salt_size, derived + (key_size * 2U),
                  XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE);
    encrypted_data = output + salt_size + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE;
    written = salt_size + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE;
    if (!xx_winzip_aes_ctr_crypt(&aes_context, input, encrypted_data, input_size,
                                 &payload_written, pd)) {
        written += payload_written;
        goto cleanup;
    }
    written += payload_written;
    if (!xx_hmac_sha1_parts(derived + key_size, key_size,
                       encrypted_data, input_size, NULL, 0U, computed_auth, pd)) goto cleanup;
    xx_bytes_copy(encrypted_data + input_size, computed_auth,
                  XX_WINZIP_AES_AUTH_CODE_SIZE);
    written += XX_WINZIP_AES_AUTH_CODE_SIZE;
    if (xx_pd_is_stopped(pd)) goto cleanup;

    if (output_size) {
        *output_size = envelope_size;
    }
    success = true;

cleanup:
    if (!success && written) xx_crypto_clear(output, written);
    xx_crypto_clear(&aes_context, sizeof(aes_context));
    xx_crypto_clear(derived, sizeof(derived));
    xx_crypto_clear(computed_auth, sizeof(computed_auth));
    return success;
}

bool xx_winzip_aes_decrypt_envelope(const uint8_t *envelope,
                                    size_t envelope_size,
                                    const uint8_t *password,
                                    size_t password_size,
                                    uint8_t strength,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_size) {
    return xx_winzip_aes_decrypt_envelope_progress(envelope,envelope_size,
        password,password_size,strength,output,output_capacity,output_size,NULL);
}

bool xx_winzip_aes_decrypt_envelope_progress(const uint8_t *envelope,
                                    size_t envelope_size,
                                    const uint8_t *password,
                                    size_t password_size,
                                    uint8_t strength,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_size,
                                    xx_pd_struct *pd) {
    uint8_t derived[XX_WINZIP_AES_MAX_DERIVED];
    uint8_t computed_auth[XX_SHA1_DIGEST_SIZE];
    xx_aes_context aes_context;
    size_t salt_size = xx_winzip_aes_salt_size(strength);
    size_t key_size = xx_winzip_aes_key_size(strength);
    size_t derived_size;
    size_t overhead;
    size_t encrypted_size;
    const uint8_t *stored_verifier;
    const uint8_t *encrypted_data;
    const uint8_t *stored_auth;
    size_t written = 0;
    bool success = false;

    if (output_size) {
        *output_size = 0U;
    }
    xx_bytes_zero(derived, sizeof(derived));
    xx_bytes_zero(computed_auth, sizeof(computed_auth));
    xx_bytes_zero((uint8_t *)&aes_context, sizeof(aes_context));
    if (xx_pd_is_stopped(pd)) goto cleanup;

    if (!envelope || salt_size == 0U || key_size == 0U ||
        (password_size > 0U && !password)) {
        goto cleanup;
    }

    overhead = salt_size + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE +
               XX_WINZIP_AES_AUTH_CODE_SIZE;
    if (envelope_size < overhead) {
        goto cleanup;
    }
    encrypted_size = envelope_size - overhead;
    if (encrypted_size > output_capacity || (encrypted_size > 0U && !output)) {
        goto cleanup;
    }

    derived_size = (key_size * 2U) + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE;
    if (derived_size > sizeof(derived) ||
        !xx_pbkdf2_hmac_sha1(password, password_size, envelope, salt_size,
                             derived, derived_size, pd)) {
        goto cleanup;
    }

    stored_verifier = envelope + salt_size;
    if (!xx_constant_time_equal(derived + (key_size * 2U), stored_verifier,
                                XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE)) {
        goto cleanup;
    }

    encrypted_data = stored_verifier + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE;
    stored_auth = encrypted_data + encrypted_size;
    if (!xx_hmac_sha1_parts(derived + key_size, key_size,
                       encrypted_data, encrypted_size, NULL, 0U, computed_auth, pd)) goto cleanup;
    if (!xx_constant_time_equal(computed_auth, stored_auth,
                                XX_WINZIP_AES_AUTH_CODE_SIZE)) {
        goto cleanup;
    }

    if (!xx_aes_internal_set_key(&aes_context, derived, key_size)) {
        goto cleanup;
    }
    if (!xx_winzip_aes_ctr_crypt(&aes_context, encrypted_data, output, encrypted_size,
                                 &written, pd)) goto cleanup;
    if (output_size) {
        *output_size = encrypted_size;
    }
    success = true;

cleanup:
    if (!success && written) xx_crypto_clear(output, written);
    xx_crypto_clear(&aes_context, sizeof(aes_context));
    xx_crypto_clear(derived, sizeof(derived));
    xx_crypto_clear(computed_auth, sizeof(computed_auth));
    return success;
}

