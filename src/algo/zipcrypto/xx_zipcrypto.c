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

#include "xxfclib/algo/zipcrypto/xx_zipcrypto.h"

typedef struct xx_zipcrypto_state {
    uint32_t key0;
    uint32_t key1;
    uint32_t key2;
} xx_zipcrypto_state;

static void xx_zipcrypto_secure_clear(void *data, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

/* One raw reflected CRC-32 update. No initial/final complement is applied. */
static uint32_t xx_zipcrypto_crc32_byte(uint32_t crc, uint8_t value) {
    unsigned int bit;
    crc ^= (uint32_t)value;
    for (bit = 0U; bit < 8U; ++bit) {
        uint32_t mask = (uint32_t)0U - (crc & 1U);
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
    return crc;
}

static void xx_zipcrypto_update_keys(xx_zipcrypto_state *state, uint8_t plain_byte) {
    state->key0 = xx_zipcrypto_crc32_byte(state->key0, plain_byte);
    state->key1 = (state->key1 + (state->key0 & 0xFFU)) * 0x08088405U + 1U;
    state->key2 = xx_zipcrypto_crc32_byte(state->key2, (uint8_t)(state->key1 >> 24U));
}

static uint8_t xx_zipcrypto_stream_byte(const xx_zipcrypto_state *state) {
    uint32_t value = (state->key2 | 2U) & 0xFFFFU;
    return (uint8_t)((value * (value ^ 1U)) >> 8U);
}

static bool xx_zipcrypto_initialize(xx_zipcrypto_state *state,
                                    const uint8_t *password,
                                    size_t password_size, xx_pd_struct *pd) {
    size_t index;
    state->key0 = 0x12345678U;
    state->key1 = 0x23456789U;
    state->key2 = 0x34567890U;

    for (index = 0U; index < password_size; ++index) {
        if ((index & 4095U) == 0 && xx_pd_is_stopped(pd)) return false;
        xx_zipcrypto_update_keys(state, password[index]);
    }
    return !xx_pd_is_stopped(pd);
}

static uint8_t xx_zipcrypto_decrypt_byte(xx_zipcrypto_state *state,
                                         uint8_t encrypted_byte) {
    uint8_t plain_byte = (uint8_t)(encrypted_byte ^ xx_zipcrypto_stream_byte(state));
    xx_zipcrypto_update_keys(state, plain_byte);
    return plain_byte;
}

static uint8_t xx_zipcrypto_encrypt_byte(xx_zipcrypto_state *state,
                                         uint8_t plain_byte) {
    uint8_t encrypted_byte = (uint8_t)(plain_byte ^ xx_zipcrypto_stream_byte(state));
    xx_zipcrypto_update_keys(state, plain_byte);
    return encrypted_byte;
}

uint8_t xx_zipcrypto_verifier_byte(uint32_t crc32,
                                   uint16_t last_mod_time,
                                   bool has_data_descriptor) {
    if (has_data_descriptor) {
        return (uint8_t)(last_mod_time >> 8U);
    }
    return (uint8_t)(crc32 >> 24U);
}

bool xx_zipcrypto_encrypt_envelope(
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
    size_t *output_size) {
    return xx_zipcrypto_encrypt_envelope_progress(input,input_size,
        password,password_size,crc32,last_mod_time,has_data_descriptor,
        random_header,output,output_capacity,output_size,NULL);
}

bool xx_zipcrypto_encrypt_envelope_progress(
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
    size_t *output_size,
    xx_pd_struct *pd) {
    xx_zipcrypto_state state;
    uint8_t header[XX_ZIPCRYPTO_HEADER_SIZE];
    size_t envelope_size;
    size_t index;
    size_t written = 0;
    bool success = false;

    if (output_size) {
        *output_size = 0U;
    }
    if (xx_pd_is_stopped(pd)) return false;
    if ((input_size > 0U && !input) ||
        (password_size > 0U && !password) || !random_header || !output ||
        input_size > SIZE_MAX - XX_ZIPCRYPTO_HEADER_SIZE) {
        return false;
    }

    envelope_size = XX_ZIPCRYPTO_HEADER_SIZE + input_size;
    if (envelope_size > output_capacity) {
        return false;
    }

    for (index = 0U; index < XX_ZIPCRYPTO_RANDOM_HEADER_SIZE; ++index) {
        header[index] = random_header[index];
    }
    header[XX_ZIPCRYPTO_HEADER_SIZE - 1U] =
        xx_zipcrypto_verifier_byte(crc32, last_mod_time, has_data_descriptor);

    if (!xx_zipcrypto_initialize(&state, password, password_size, pd)) goto cleanup;
    for (index = 0U; index < XX_ZIPCRYPTO_HEADER_SIZE; ++index) {
        output[index] = xx_zipcrypto_encrypt_byte(&state, header[index]);
    }
    written = XX_ZIPCRYPTO_HEADER_SIZE;
    for (index = 0U; index < input_size; ++index) {
        if ((index & 4095U) == 0 && xx_pd_is_stopped(pd)) goto cleanup;
        output[XX_ZIPCRYPTO_HEADER_SIZE + index] =
            xx_zipcrypto_encrypt_byte(&state, input[index]);
        ++written;
    }
    if (xx_pd_is_stopped(pd)) goto cleanup;

    if (output_size) {
        *output_size = envelope_size;
    }
    success = true;
cleanup:
    if (!success && written) xx_zipcrypto_secure_clear(output, written);
    xx_zipcrypto_secure_clear(header, sizeof(header));
    xx_zipcrypto_secure_clear(&state, sizeof(state));
    return success;
}

bool xx_zipcrypto_decrypt_envelope(const uint8_t *envelope,
                                   size_t envelope_size,
                                   const uint8_t *password,
                                   size_t password_size,
                                   uint32_t crc32,
                                   uint16_t last_mod_time,
                                   bool has_data_descriptor,
                                   uint8_t *output,
                                   size_t output_capacity,
                                   size_t *output_size) {
    return xx_zipcrypto_decrypt_envelope_progress(envelope,envelope_size,
        password,password_size,crc32,last_mod_time,has_data_descriptor,
        output,output_capacity,output_size,NULL);
}

bool xx_zipcrypto_decrypt_envelope_progress(const uint8_t *envelope,
                                   size_t envelope_size,
                                   const uint8_t *password,
                                   size_t password_size,
                                   uint32_t crc32,
                                   uint16_t last_mod_time,
                                   bool has_data_descriptor,
                                   uint8_t *output,
                                   size_t output_capacity,
                                   size_t *output_size,
                                   xx_pd_struct *pd) {
    xx_zipcrypto_state state;
    uint8_t header[XX_ZIPCRYPTO_HEADER_SIZE];
    size_t payload_size;
    size_t index;
    size_t written = 0;
    bool success = false;

    if (output_size) {
        *output_size = 0U;
    }
    if (xx_pd_is_stopped(pd)) return false;

    if (!envelope || envelope_size < XX_ZIPCRYPTO_HEADER_SIZE ||
        (password_size > 0U && !password)) {
        return false;
    }

    payload_size = envelope_size - XX_ZIPCRYPTO_HEADER_SIZE;
    if (payload_size > output_capacity || (payload_size > 0U && !output)) {
        return false;
    }

    if (!xx_zipcrypto_initialize(&state, password, password_size, pd)) goto cleanup;

    for (index = 0U; index < XX_ZIPCRYPTO_HEADER_SIZE; ++index) {
        header[index] = xx_zipcrypto_decrypt_byte(&state, envelope[index]);
    }

    if (header[XX_ZIPCRYPTO_HEADER_SIZE - 1U] !=
        xx_zipcrypto_verifier_byte(crc32, last_mod_time, has_data_descriptor)) {
        goto cleanup;
    }

    for (index = 0U; index < payload_size; ++index) {
        if ((index & 4095U) == 0 && xx_pd_is_stopped(pd)) goto cleanup;
        output[index] = xx_zipcrypto_decrypt_byte(
            &state, envelope[XX_ZIPCRYPTO_HEADER_SIZE + index]);
        ++written;
    }
    if (xx_pd_is_stopped(pd)) goto cleanup;

    if (output_size) {
        *output_size = payload_size;
    }
    success = true;

cleanup:
    if (!success && written) xx_zipcrypto_secure_clear(output, written);
    xx_zipcrypto_secure_clear(header, sizeof(header));
    xx_zipcrypto_secure_clear(&state, sizeof(state));
    return success;
}
