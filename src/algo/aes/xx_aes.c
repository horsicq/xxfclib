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

#include "xxfclib/algo/aes/xx_aes.h"
#include "xx_aes_internal.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "../../io/platforms/xx_io_platform.h"

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

static uint32_t xx_rotate_left32(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t xx_load_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static uint8_t xx_aes_xtime(uint8_t value) {
    return (uint8_t)((uint8_t)(value << 1U) ^
                     (uint8_t)(0x1BU & (uint8_t)(0U - (uint8_t)(value >> 7U))));
}

static uint8_t xx_aes_gf_multiply(uint8_t left, uint8_t right) {
    uint8_t result = 0U;
    unsigned int bit;
    for (bit = 0U; bit < 8U; ++bit) {
        uint8_t mask = (uint8_t)(0U - (uint8_t)(right & 1U));
        result ^= (uint8_t)(left & mask);
        left = xx_aes_xtime(left);
        right >>= 1U;
    }
    return result;
}

static uint8_t xx_aes_gf_inverse(uint8_t value) {
    uint8_t result = 1U;
    uint8_t base = value;
    unsigned int exponent = 254U;

    if (value == 0U) {
        return 0U;
    }
    while (exponent > 0U) {
        if ((exponent & 1U) != 0U) {
            result = xx_aes_gf_multiply(result, base);
        }
        base = xx_aes_gf_multiply(base, base);
        exponent >>= 1U;
    }
    return result;
}

static uint8_t xx_rotate_left8(uint8_t value, unsigned int count) {
    return (uint8_t)(((uint32_t)value << count) |
                     ((uint32_t)value >> (8U - count)));
}

static uint8_t xx_aes_calculate_sbox(uint8_t value) {
    uint8_t inverse = xx_aes_gf_inverse(value);
    return (uint8_t)(inverse ^ xx_rotate_left8(inverse, 1U) ^
                     xx_rotate_left8(inverse, 2U) ^
                     xx_rotate_left8(inverse, 3U) ^
                     xx_rotate_left8(inverse, 4U) ^ 0x63U);
}

bool xx_aes_internal_set_key(xx_aes_context *context,
                           const uint8_t *key, size_t key_size) {
    size_t generated;
    size_t round_key_size;
    uint8_t rcon = 1U;
    unsigned int index;

    if (!context || !key ||
        (key_size != 16U && key_size != 24U && key_size != 32U)) {
        return false;
    }

    for (index = 0U; index < 256U; ++index) {
        context->sbox[index] = xx_aes_calculate_sbox((uint8_t)index);
    }
    for (index = 0U; index < 256U; ++index) {
        context->inverse_sbox[context->sbox[index]] = (uint8_t)index;
    }
    context->rounds = (unsigned int)(key_size / 4U) + 6U;
    round_key_size = ((size_t)context->rounds + 1U) * XX_AES_BLOCK_SIZE;
    xx_bytes_copy(context->round_keys, key, key_size);
    generated = key_size;

    while (generated < round_key_size) {
        uint8_t temporary[4];
        temporary[0] = context->round_keys[generated - 4U];
        temporary[1] = context->round_keys[generated - 3U];
        temporary[2] = context->round_keys[generated - 2U];
        temporary[3] = context->round_keys[generated - 1U];

        if ((generated % key_size) == 0U) {
            uint8_t rotated = temporary[0];
            temporary[0] = context->sbox[temporary[1]];
            temporary[1] = context->sbox[temporary[2]];
            temporary[2] = context->sbox[temporary[3]];
            temporary[3] = context->sbox[rotated];
            temporary[0] ^= rcon;
            rcon = xx_aes_xtime(rcon);
        } else if (key_size == 32U && (generated % key_size) == 16U) {
            temporary[0] = context->sbox[temporary[0]];
            temporary[1] = context->sbox[temporary[1]];
            temporary[2] = context->sbox[temporary[2]];
            temporary[3] = context->sbox[temporary[3]];
        }

        for (index = 0U; index < 4U && generated < round_key_size; ++index) {
            context->round_keys[generated] =
                (uint8_t)(context->round_keys[generated - key_size] ^ temporary[index]);
            ++generated;
        }
        xx_crypto_clear(temporary, sizeof(temporary));
    }
    return true;
}

static void xx_aes_add_round_key(uint8_t state[XX_AES_BLOCK_SIZE],
                                 const uint8_t *round_key) {
    unsigned int index;
    for (index = 0U; index < XX_AES_BLOCK_SIZE; ++index) {
        state[index] ^= round_key[index];
    }
}

static void xx_aes_sub_bytes(uint8_t state[XX_AES_BLOCK_SIZE],
                             const uint8_t sbox[256]) {
    unsigned int index;
    for (index = 0U; index < XX_AES_BLOCK_SIZE; ++index) {
        state[index] = sbox[state[index]];
    }
}

static void xx_aes_shift_rows(uint8_t state[XX_AES_BLOCK_SIZE]) {
    uint8_t temporary;

    temporary = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temporary;
}

static void xx_aes_mix_columns(uint8_t state[XX_AES_BLOCK_SIZE]) {
    unsigned int column;
    for (column = 0U; column < 4U; ++column) {
        uint8_t *values = state + ((size_t)column * 4U);
        uint8_t first = values[0];
        uint8_t combined = (uint8_t)(values[0] ^ values[1] ^ values[2] ^ values[3]);
        values[0] ^= (uint8_t)(combined ^ xx_aes_xtime((uint8_t)(values[0] ^ values[1])));
        values[1] ^= (uint8_t)(combined ^ xx_aes_xtime((uint8_t)(values[1] ^ values[2])));
        values[2] ^= (uint8_t)(combined ^ xx_aes_xtime((uint8_t)(values[2] ^ values[3])));
        values[3] ^= (uint8_t)(combined ^ xx_aes_xtime((uint8_t)(values[3] ^ first)));
    }
}

void xx_aes_internal_encrypt_block(const xx_aes_context *context,
                                 const uint8_t input[XX_AES_BLOCK_SIZE],
                                 uint8_t output[XX_AES_BLOCK_SIZE]) {
    uint8_t state[XX_AES_BLOCK_SIZE];
    unsigned int round;

    xx_bytes_copy(state, input, sizeof(state));
    xx_aes_add_round_key(state, context->round_keys);

    for (round = 1U; round < context->rounds; ++round) {
        xx_aes_sub_bytes(state, context->sbox);
        xx_aes_shift_rows(state);
        xx_aes_mix_columns(state);
        xx_aes_add_round_key(state,
                             context->round_keys + ((size_t)round * XX_AES_BLOCK_SIZE));
    }

    xx_aes_sub_bytes(state, context->sbox);
    xx_aes_shift_rows(state);
    xx_aes_add_round_key(state,
                         context->round_keys + ((size_t)context->rounds * XX_AES_BLOCK_SIZE));
    xx_bytes_copy(output, state, sizeof(state));
    xx_crypto_clear(state, sizeof(state));
}

static void xx_aes_inverse_shift_rows(uint8_t state[XX_AES_BLOCK_SIZE]) {
    uint8_t temporary;

    temporary = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temporary;

    temporary = state[2];
    state[2] = state[10];
    state[10] = temporary;
    temporary = state[6];
    state[6] = state[14];
    state[14] = temporary;

    temporary = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temporary;
}

static void xx_aes_inverse_sub_bytes(uint8_t state[XX_AES_BLOCK_SIZE],
                                     const uint8_t inverse_sbox[256]) {
    unsigned int index;
    for (index = 0U; index < XX_AES_BLOCK_SIZE; ++index) {
        state[index] = inverse_sbox[state[index]];
    }
}

static void xx_aes_inverse_mix_columns(uint8_t state[XX_AES_BLOCK_SIZE]) {
    unsigned int column;
    for (column = 0U; column < 4U; ++column) {
        uint8_t *values = state + ((size_t)column * 4U);
        uint8_t a = values[0];
        uint8_t b = values[1];
        uint8_t c = values[2];
        uint8_t d = values[3];
        values[0] = xx_aes_gf_multiply(a, 14U) ^
                    xx_aes_gf_multiply(b, 11U) ^
                    xx_aes_gf_multiply(c, 13U) ^
                    xx_aes_gf_multiply(d, 9U);
        values[1] = xx_aes_gf_multiply(a, 9U) ^
                    xx_aes_gf_multiply(b, 14U) ^
                    xx_aes_gf_multiply(c, 11U) ^
                    xx_aes_gf_multiply(d, 13U);
        values[2] = xx_aes_gf_multiply(a, 13U) ^
                    xx_aes_gf_multiply(b, 9U) ^
                    xx_aes_gf_multiply(c, 14U) ^
                    xx_aes_gf_multiply(d, 11U);
        values[3] = xx_aes_gf_multiply(a, 11U) ^
                    xx_aes_gf_multiply(b, 13U) ^
                    xx_aes_gf_multiply(c, 9U) ^
                    xx_aes_gf_multiply(d, 14U);
    }
}

static void xx_aes_decrypt_block(const xx_aes_context *context,
                                 const uint8_t input[XX_AES_BLOCK_SIZE],
                                 uint8_t output[XX_AES_BLOCK_SIZE]) {
    uint8_t state[XX_AES_BLOCK_SIZE];
    unsigned int round;

    xx_bytes_copy(state, input, sizeof(state));
    xx_aes_add_round_key(state,
                         context->round_keys + ((size_t)context->rounds * XX_AES_BLOCK_SIZE));
    for (round = context->rounds - 1U; round > 0U; --round) {
        xx_aes_inverse_shift_rows(state);
        xx_aes_inverse_sub_bytes(state, context->inverse_sbox);
        xx_aes_add_round_key(state,
                             context->round_keys + ((size_t)round * XX_AES_BLOCK_SIZE));
        xx_aes_inverse_mix_columns(state);
    }
    xx_aes_inverse_shift_rows(state);
    xx_aes_inverse_sub_bytes(state, context->inverse_sbox);
    xx_aes_add_round_key(state, context->round_keys);
    xx_bytes_copy(output, state, sizeof(state));
    xx_crypto_clear(state, sizeof(state));
}

static bool xx_7zip_aes_derive_key(const uint8_t *password,
                                   size_t password_size,
                                   const uint8_t *salt,
                                   size_t salt_size,
                                   uint8_t cycles_power,
                                   uint8_t key[XX_SHA256_DIGEST_SIZE],
                                   xx_pd_struct *pd) {
    xx_sha256_context hash;
    uint8_t counter[8];
    uint32_t rounds;
    uint32_t round;

    if ((password_size > 0U && !password) || (salt_size > 0U && !salt) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_bytes_zero(key, XX_SHA256_DIGEST_SIZE);
    if (cycles_power == 0x3FU) {
        size_t copied = salt_size;
        if (copied > XX_SHA256_DIGEST_SIZE) copied = XX_SHA256_DIGEST_SIZE;
        if (copied > 0U) xx_bytes_copy(key, salt, copied);
        if (copied < XX_SHA256_DIGEST_SIZE) {
            size_t password_copy = password_size;
            if (password_copy > XX_SHA256_DIGEST_SIZE - copied) {
                password_copy = XX_SHA256_DIGEST_SIZE - copied;
            }
            if (password_copy > 0U) {
                xx_bytes_copy(key + copied, password, password_copy);
            }
        }
        return true;
    }
    if (cycles_power > 24U) return false;

    rounds = UINT32_C(1) << cycles_power;
    xx_sha256_init(&hash);
    xx_bytes_zero(counter, sizeof(counter));
    for (round = 0U; round < rounds; ++round) {
        unsigned int index;
        if ((round & 1023U) == 0U && pd && xx_pd_is_stopped(pd)) {
            xx_crypto_clear(&hash, sizeof(hash));
            xx_crypto_clear(counter, sizeof(counter));
            return false;
        }
        for (index = 0U; index < 4U; ++index) {
            counter[index] = (uint8_t)(round >> (8U * index));
        }
        if (salt_size > 0U) xx_sha256_update(&hash, salt, salt_size);
        if (password_size > 0U) xx_sha256_update(&hash, password, password_size);
        xx_sha256_update(&hash, counter, sizeof(counter));
    }
    xx_sha256_final(&hash, key, XX_SHA256_DIGEST_SIZE);
    xx_crypto_clear(&hash, sizeof(hash));
    xx_crypto_clear(counter, sizeof(counter));
    return true;
}

bool xx_7zip_aes_decrypt(const uint8_t *input,
                         size_t input_size,
                         const uint8_t *password_utf16le,
                         size_t password_size,
                         const uint8_t *properties,
                         size_t properties_size,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t plaintext_size) {
    xx_aes_context aes_context;
    uint8_t key[XX_SHA256_DIGEST_SIZE];
    uint8_t iv[XX_AES_BLOCK_SIZE];
    uint8_t previous[XX_AES_BLOCK_SIZE];
    uint8_t ciphertext[XX_AES_BLOCK_SIZE];
    uint8_t plaintext[XX_AES_BLOCK_SIZE];
    const uint8_t *salt = NULL;
    size_t salt_size = 0U;
    size_t iv_size = 0U;
    size_t property_offset = 1U;
    size_t input_offset = 0U;
    size_t output_offset = 0U;
    uint8_t cycles_power;
    bool success = false;

    xx_bytes_zero((uint8_t *)&aes_context, sizeof(aes_context));
    xx_bytes_zero(key, sizeof(key));
    xx_bytes_zero(iv, sizeof(iv));
    xx_bytes_zero(previous, sizeof(previous));
    xx_bytes_zero(ciphertext, sizeof(ciphertext));
    xx_bytes_zero(plaintext, sizeof(plaintext));

    if (!input || !properties || properties_size < 1U ||
        (password_size > 0U && !password_utf16le) ||
        (plaintext_size > 0U && !output) || plaintext_size > output_capacity ||
        (input_size & (XX_AES_BLOCK_SIZE - 1U)) != 0U ||
        plaintext_size > input_size) {
        goto cleanup;
    }

    cycles_power = properties[0] & 0x3FU;
    if ((properties[0] & 0xC0U) != 0U) {
        uint8_t second;
        if (properties_size < 2U) goto cleanup;
        second = properties[1];
        salt_size = (size_t)((properties[0] >> 7U) & 1U) +
                    (size_t)(second >> 4U);
        iv_size = (size_t)((properties[0] >> 6U) & 1U) +
                  (size_t)(second & 0x0FU);
        property_offset = 2U;
    }
    if (salt_size > XX_AES_BLOCK_SIZE || iv_size > XX_AES_BLOCK_SIZE ||
        property_offset + salt_size + iv_size != properties_size) {
        goto cleanup;
    }
    salt = properties + property_offset;
    if (iv_size > 0U) {
        xx_bytes_copy(iv, properties + property_offset + salt_size, iv_size);
    }
    if (!xx_7zip_aes_derive_key(password_utf16le, password_size,
                                 salt, salt_size, cycles_power, key, NULL) ||
        !xx_aes_internal_set_key(&aes_context, key, sizeof(key))) {
        goto cleanup;
    }

    xx_bytes_copy(previous, iv, sizeof(previous));
    while (output_offset < plaintext_size) {
        size_t count = plaintext_size - output_offset;
        unsigned int index;
        if (input_offset > input_size - XX_AES_BLOCK_SIZE) goto cleanup;
        if (count > XX_AES_BLOCK_SIZE) count = XX_AES_BLOCK_SIZE;
        xx_bytes_copy(ciphertext, input + input_offset, sizeof(ciphertext));
        xx_aes_decrypt_block(&aes_context, ciphertext, plaintext);
        for (index = 0U; index < XX_AES_BLOCK_SIZE; ++index) {
            plaintext[index] ^= previous[index];
        }
        xx_bytes_copy(output + output_offset, plaintext, count);
        xx_bytes_copy(previous, ciphertext, sizeof(previous));
        input_offset += XX_AES_BLOCK_SIZE;
        output_offset += count;
    }
    success = true;

cleanup:
    xx_crypto_clear(&aes_context, sizeof(aes_context));
    xx_crypto_clear(key, sizeof(key));
    xx_crypto_clear(iv, sizeof(iv));
    xx_crypto_clear(previous, sizeof(previous));
    xx_crypto_clear(ciphertext, sizeof(ciphertext));
    xx_crypto_clear(plaintext, sizeof(plaintext));
    return success;
}

static bool xx_7zip_aes_read_exact(xx_io_device *source, uint8_t *data, size_t size, xx_pd_struct *pd) {
    while (size) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        ssize_t count = xx_io_read(source, data, size);
        if (count <= 0 || (size_t)count > size) return false;
        data += count; size -= (size_t)count;
    }
    return true;
}

static bool xx_7zip_aes_crypt_device(xx_aes_context *aes, uint8_t previous[16],
    bool encrypt, xx_io_device *source, int64_t source_offset, int64_t input_size,
    int64_t plaintext_size, xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t input[8192], output[8192];
    int64_t remaining = input_size, meaningful = plaintext_size;
    bool result = false;
    if (xx_io_seek64(source, source_offset, SEEK_SET) != 0) goto cleanup;
    while (remaining > 0) {
        size_t count = remaining > (int64_t)sizeof(input) ? sizeof(input) : (size_t)remaining;
        size_t padded = encrypt ? (count + 15U) & ~(size_t)15U : count;
        size_t at, written;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_7zip_aes_read_exact(source, input, count, pd) ||
            (pd && xx_pd_is_stopped(pd))) goto cleanup;
        if (padded > count) xx_bytes_zero(input + count, padded - count);
        for (at = 0U; at < padded; at += 16U) {
            unsigned int i;
            if (encrypt) {
                for (i = 0U; i < 16U; ++i) input[at + i] ^= previous[i];
                xx_aes_internal_encrypt_block(aes, input + at, output + at);
                xx_bytes_copy(previous, output + at, 16U);
            } else {
                xx_aes_decrypt_block(aes, input + at, output + at);
                for (i = 0U; i < 16U; ++i) output[at + i] ^= previous[i];
                xx_bytes_copy(previous, input + at, 16U);
            }
        }
        written = encrypt ? padded : (meaningful < (int64_t)count ? (size_t)meaningful : count);
        if (written && xx_io_write(destination, output, written) != (ssize_t)written) goto cleanup;
        remaining -= (int64_t)count;
        if (!encrypt) meaningful -= (int64_t)written;
    }
    result = !(pd && xx_pd_is_stopped(pd)) && (encrypt || meaningful == 0);
cleanup:
    xx_crypto_clear(input, sizeof(input));
    xx_crypto_clear(output, sizeof(output));
    return result;
}

bool xx_7zip_aes_encrypt_device(xx_io_device *source, int64_t source_offset,
    int64_t plaintext_size, const uint8_t *password_utf16le, size_t password_size,
    uint8_t properties[34], size_t *properties_size, xx_io_device *destination,
    int64_t *output_size, xx_pd_struct *pd) {
    xx_aes_context aes;
    uint8_t key[32], previous[16];
    bool result = false;
    xx_bytes_zero((uint8_t *)&aes, sizeof(aes));
    xx_bytes_zero(key, sizeof(key)); xx_bytes_zero(previous, sizeof(previous));
    if (properties_size) *properties_size = 0U;
    if (output_size) *output_size = 0;
    if (!source || !destination || !properties || !properties_size ||
        source == destination || source_offset < 0 || plaintext_size < 0 ||
        plaintext_size > INT64_MAX - 15 || source_offset > INT64_MAX - plaintext_size ||
        (password_size && !password_utf16le) || (password_size & 1U) ||
        (pd && xx_pd_is_stopped(pd))) goto cleanup;
    properties[0] = 0xD3U; properties[1] = 0xFFU;
    if (!xx_io_platform_secure_random(properties + 2U, 32U) ||
        !xx_7zip_aes_derive_key(password_utf16le, password_size, properties + 2U,
            16U, 19U, key, pd) || !xx_aes_internal_set_key(&aes, key, sizeof(key))) goto cleanup;
    xx_bytes_copy(previous, properties + 18U, 16U);
    result = xx_7zip_aes_crypt_device(&aes, previous, true, source, source_offset,
        plaintext_size, plaintext_size, destination, pd);
    if (result) {
        *properties_size = 34U;
        if (output_size) *output_size = (plaintext_size + 15) & ~(int64_t)15;
    }
cleanup:
    if (!result && properties) xx_crypto_clear(properties, 34U);
    xx_crypto_clear(&aes, sizeof(aes)); xx_crypto_clear(key, sizeof(key));
    xx_crypto_clear(previous, sizeof(previous));
    return result;
}

bool xx_7zip_aes_decrypt_device(xx_io_device *source, int64_t source_offset,
    int64_t input_size, const uint8_t *password_utf16le, size_t password_size,
    const uint8_t *properties, size_t properties_size, int64_t plaintext_size,
    xx_io_device *destination, xx_pd_struct *pd) {
    xx_aes_context aes;
    uint8_t key[32], previous[16];
    size_t salt_size = 0U, iv_size = 0U, at = 1U;
    uint8_t cycles;
    bool result = false;
    xx_bytes_zero((uint8_t *)&aes, sizeof(aes));
    xx_bytes_zero(key, sizeof(key)); xx_bytes_zero(previous, sizeof(previous));
    if (!source || !destination || source == destination || !properties || !properties_size ||
        source_offset < 0 || input_size < 0 || (input_size & 15) || plaintext_size < 0 ||
        plaintext_size > input_size || input_size - plaintext_size > 15 ||
        source_offset > INT64_MAX - input_size || (password_size & 1U) ||
        (password_size && !password_utf16le) || (pd && xx_pd_is_stopped(pd))) goto cleanup;
    cycles = properties[0] & 0x3FU;
    if (properties[0] & 0xC0U) {
        if (properties_size < 2U) goto cleanup;
        salt_size = ((properties[0] >> 7U) & 1U) + (properties[1] >> 4U);
        iv_size = ((properties[0] >> 6U) & 1U) + (properties[1] & 15U);
        at = 2U;
    }
    if (salt_size > 16U || iv_size > 16U || at + salt_size + iv_size != properties_size ||
        !xx_7zip_aes_derive_key(password_utf16le, password_size,
            properties + at, salt_size, cycles, key, pd) ||
        !xx_aes_internal_set_key(&aes, key, sizeof(key))) goto cleanup;
    xx_bytes_copy(previous, properties + at + salt_size, iv_size);
    result = xx_7zip_aes_crypt_device(&aes, previous, false, source, source_offset,
        input_size, plaintext_size, destination, pd);
cleanup:
    xx_crypto_clear(&aes, sizeof(aes)); xx_crypto_clear(key, sizeof(key));
    xx_crypto_clear(previous, sizeof(previous));
    return result;
}

/* RAR uses CBC with archive-defined framing and integrity, rather than the
 * WinZip authenticated envelope. Keep those decisions with the format parser. */
bool xx_aes_cbc_decrypt(const uint8_t *input, size_t input_size,
                         const uint8_t *key, size_t key_size,
                         const uint8_t iv16[16], uint8_t *output) {
    xx_aes_context aes;
    uint8_t previous[16], encrypted[16], plain[16];
    size_t at;
    unsigned i;
    if (!key || !iv16 || (input_size & 15U) ||
        (input_size && (!input || !output)) ||
        (key_size != 16U && key_size != 24U && key_size != 32U)) return false;
    xx_bytes_zero((uint8_t *)&aes, sizeof(aes));
    if (!xx_aes_internal_set_key(&aes, key, key_size)) {
        xx_crypto_clear(&aes, sizeof(aes));
        return false;
    }
    xx_bytes_copy(previous, iv16, sizeof(previous));
    for (at = 0; at < input_size; at += 16U) {
        xx_bytes_copy(encrypted, input + at, sizeof(encrypted));
        xx_aes_decrypt_block(&aes, encrypted, plain);
        for (i = 0; i < 16U; ++i) output[at + i] = plain[i] ^ previous[i];
        xx_bytes_copy(previous, encrypted, sizeof(previous));
    }
    xx_crypto_clear(&aes, sizeof(aes));
    xx_crypto_clear(previous, sizeof(previous));
    xx_crypto_clear(encrypted, sizeof(encrypted));
    xx_crypto_clear(plain, sizeof(plain));
    return true;
}

/* Historical RAR SHA-1 updated directly processed password blocks in place.
 * Reproduce its on-disk KDF consequence without changing the normal SHA-1
 * implementation: the final sixteen schedule words become little-endian
 * input bytes for later rounds. */
static void xx_rar3_password_block(uint8_t block[64]) {
    uint32_t schedule[80];
    unsigned i, byte;
    for (i = 0; i < 16U; ++i) schedule[i] = xx_load_be32(block + i * 4U);
    for (i = 16U; i < 80U; ++i)
        schedule[i] = xx_rotate_left32(schedule[i - 3U] ^ schedule[i - 8U] ^
                                       schedule[i - 14U] ^ schedule[i - 16U], 1U);
    for (i = 0; i < 16U; ++i)
        for (byte = 0; byte < 4U; ++byte)
            block[i * 4U + byte] = (uint8_t)(schedule[64U + i] >> (8U * byte));
    xx_crypto_clear(schedule, sizeof(schedule));
}

bool xx_rar3_aes_derive(const uint8_t *password_utf16le, size_t password_size,
                         const uint8_t *salt8, uint8_t key16[16],
                         uint8_t iv16[16], xx_pd_struct *pd) {
    xx_sha1_context hash, snapshot;
    uint8_t raw[254U + 8U], digest[20], counter[3], iv[16];
    uint32_t round;
    size_t raw_size, direct_block;
    unsigned word, byte;
    bool success = false;
    if (key16) xx_crypto_clear(key16, 16);
    if (iv16) xx_crypto_clear(iv16, 16);
    if (!key16 || !iv16 || (password_size && !password_utf16le) ||
        (password_size & 1U) || xx_pd_is_stopped(pd)) return false;
    if (password_size > 254U) password_size = 254U;
    raw_size = password_size + (salt8 ? 8U : 0U);
    xx_bytes_zero(raw, sizeof(raw));
    xx_bytes_copy(raw, password_utf16le, password_size);
    if (salt8) xx_bytes_copy(raw + password_size, salt8, 8);
    xx_sha1_init(&hash);
    for (round = 0; round < UINT32_C(0x40000); ++round) {
        if ((round & 1023U) == 0 && xx_pd_is_stopped(pd)) goto cleanup;
        /* The first completed block goes through SHA-1's private buffer; only
         * subsequent complete blocks within this password update were mutable. */
        direct_block = 64U - hash.buffer_size;
        xx_sha1_update(&hash, raw, raw_size);
        while (direct_block + 64U <= raw_size) {
            xx_rar3_password_block(raw + direct_block);
            direct_block += 64U;
        }
        counter[0] = (uint8_t)round;
        counter[1] = (uint8_t)(round >> 8);
        counter[2] = (uint8_t)(round >> 16);
        xx_sha1_update(&hash, counter, sizeof(counter));
        if ((round & 16383U) == 0) {
            snapshot = hash;
            xx_sha1_final(&snapshot, digest, XX_SHA1_DIGEST_SIZE);
            iv[round >> 14] = digest[19];
        }
    }
    if (xx_pd_is_stopped(pd)) goto cleanup;
    xx_sha1_final(&hash, digest, XX_SHA1_DIGEST_SIZE);
    for (word = 0; word < 4U; ++word)
        for (byte = 0; byte < 4U; ++byte)
            key16[word * 4U + byte] = digest[word * 4U + 3U - byte];
    xx_bytes_copy(iv16, iv, sizeof(iv));
    success = true;
cleanup:
    xx_crypto_clear(&hash, sizeof(hash));
    xx_crypto_clear(&snapshot, sizeof(snapshot));
    xx_crypto_clear(raw, sizeof(raw));
    xx_crypto_clear(digest, sizeof(digest));
    xx_crypto_clear(counter, sizeof(counter));
    xx_crypto_clear(iv, sizeof(iv));
    return success;
}

typedef struct xx_rar5_hmac_base {
    xx_sha256_context inner;
    xx_sha256_context outer;
} xx_rar5_hmac_base;

static void xx_rar5_hmac_init(xx_rar5_hmac_base *base,
                              const uint8_t *key, size_t key_size) {
    uint8_t material[64], pad[64];
    xx_sha256_context hash;
    unsigned i;
    xx_bytes_zero(material, sizeof(material));
    if (key_size > 64U) {
        xx_sha256_init(&hash);
        xx_sha256_update(&hash, key, key_size);
        xx_sha256_final(&hash, material, XX_SHA256_DIGEST_SIZE);
        xx_crypto_clear(&hash, sizeof(hash));
    } else xx_bytes_copy(material, key, key_size);
    for (i = 0; i < 64U; ++i) pad[i] = material[i] ^ 0x36U;
    xx_sha256_init(&base->inner);
    xx_sha256_update(&base->inner, pad, sizeof(pad));
    for (i = 0; i < 64U; ++i) pad[i] = material[i] ^ 0x5cU;
    xx_sha256_init(&base->outer);
    xx_sha256_update(&base->outer, pad, sizeof(pad));
    xx_crypto_clear(material, sizeof(material));
    xx_crypto_clear(pad, sizeof(pad));
}

static void xx_rar5_hmac_digest(const xx_rar5_hmac_base *base,
                                const uint8_t *bytes, size_t size,
                                uint8_t digest[32]) {
    xx_sha256_context hash = base->inner;
    uint8_t inner[32];
    xx_sha256_update(&hash, bytes, size);
    xx_sha256_final(&hash, inner, XX_SHA256_DIGEST_SIZE);
    hash = base->outer;
    xx_sha256_update(&hash, inner, sizeof(inner));
    xx_sha256_final(&hash, digest, XX_SHA256_DIGEST_SIZE);
    xx_crypto_clear(&hash, sizeof(hash));
    xx_crypto_clear(inner, sizeof(inner));
}

bool xx_rar5_aes_derive(const uint8_t *password_utf8, size_t password_size,
                         const uint8_t salt16[16], uint8_t kdf_log,
                         uint8_t key32[32], uint8_t hash_key32[32],
                         uint8_t check8[8], xx_pd_struct *pd) {
    xx_rar5_hmac_base hmac;
    uint8_t seed[20], link[32], accumulated[32];
    uint32_t remaining, iteration;
    unsigned phase, i;
    bool success = false;
    if (key32) xx_crypto_clear(key32, 32);
    if (hash_key32) xx_crypto_clear(hash_key32, 32);
    if (check8) xx_crypto_clear(check8, 8);
    if (!key32 || !hash_key32 || !check8 || !salt16 || kdf_log > 24U ||
        (password_size && !password_utf8) || xx_pd_is_stopped(pd)) return false;
    xx_rar5_hmac_init(&hmac, password_utf8, password_size);
    xx_bytes_copy(seed, salt16, 16);
    seed[16] = 0; seed[17] = 0; seed[18] = 0; seed[19] = 1;
    xx_rar5_hmac_digest(&hmac, seed, sizeof(seed), link);
    xx_bytes_copy(accumulated, link, sizeof(accumulated));
    remaining = (UINT32_C(1) << kdf_log) - 1U;
    for (phase = 0; phase < 3U; ++phase) {
        for (iteration = 0; iteration < remaining; ++iteration) {
            if ((iteration & 1023U) == 0 && xx_pd_is_stopped(pd)) goto cleanup;
            xx_rar5_hmac_digest(&hmac, link, sizeof(link), link);
            for (i = 0; i < 32U; ++i) accumulated[i] ^= link[i];
        }
        if (phase == 0) xx_bytes_copy(key32, accumulated, 32);
        else if (phase == 1) xx_bytes_copy(hash_key32, accumulated, 32);
        else {
            for (i = 0; i < 8U; ++i)
                check8[i] = accumulated[i] ^ accumulated[i + 8U] ^
                            accumulated[i + 16U] ^ accumulated[i + 24U];
        }
        remaining = 16U;
    }
    success = !xx_pd_is_stopped(pd);
cleanup:
    if (!success) {
        xx_crypto_clear(key32, 32);
        xx_crypto_clear(hash_key32, 32);
        xx_crypto_clear(check8, 8);
    }
    xx_crypto_clear(&hmac, sizeof(hmac));
    xx_crypto_clear(seed, sizeof(seed));
    xx_crypto_clear(link, sizeof(link));
    xx_crypto_clear(accumulated, sizeof(accumulated));
    return success;
}

bool xx_rar5_aes_check_password(const uint8_t stored12[12],
                                 const uint8_t derived8[8]) {
    xx_sha256_context hash;
    uint8_t checksum[32];
    volatile unsigned difference = 0;
    unsigned i;
    if (!stored12 || !derived8) return false;
    xx_sha256_init(&hash);
    xx_sha256_update(&hash, stored12, 8);
    xx_sha256_final(&hash, checksum, XX_SHA256_DIGEST_SIZE);
    for (i = 0; i < 8U; ++i) difference |= stored12[i] ^ derived8[i];
    for (i = 0; i < 4U; ++i) difference |= stored12[i + 8U] ^ checksum[i];
    xx_crypto_clear(&hash, sizeof(hash));
    xx_crypto_clear(checksum, sizeof(checksum));
    return difference == 0;
}

uint32_t xx_rar5_aes_mac_crc32(const uint8_t hash_key32[32], uint32_t crc32) {
    xx_rar5_hmac_base hmac;
    uint8_t bytes[4], digest[32];
    uint32_t result = 0;
    unsigned i;
    if (!hash_key32) return 0;
    for (i = 0; i < 4U; ++i) bytes[i] = (uint8_t)(crc32 >> (8U * i));
    xx_rar5_hmac_init(&hmac, hash_key32, 32);
    xx_rar5_hmac_digest(&hmac, bytes, sizeof(bytes), digest);
    for (i = 0; i < 32U; ++i) result ^= (uint32_t)digest[i] << (8U * (i & 3U));
    xx_crypto_clear(&hmac, sizeof(hmac));
    xx_crypto_clear(bytes, sizeof(bytes));
    xx_crypto_clear(digest, sizeof(digest));
    return result;
}

bool xx_rar5_aes_mac_hash(const uint8_t hash_key32[32],
                           const uint8_t digest32[32], uint8_t output32[32]) {
    xx_rar5_hmac_base hmac;
    if (!hash_key32 || !digest32 || !output32) {
        if (output32) xx_crypto_clear(output32, 32);
        return false;
    }
    xx_rar5_hmac_init(&hmac, hash_key32, 32);
    xx_rar5_hmac_digest(&hmac, digest32, 32, output32);
    xx_crypto_clear(&hmac, sizeof(hmac));
    return true;
}
