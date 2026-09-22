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

#define XX_SHA1_BLOCK_SIZE          64U
#define XX_SHA1_DIGEST_SIZE         20U
#define XX_SHA256_BLOCK_SIZE        64U
#define XX_SHA256_DIGEST_SIZE       32U
#define XX_AES_BLOCK_SIZE           16U
#define XX_AES_MAX_ROUND_KEY_SIZE   240U
#define XX_WINZIP_AES_MAX_DERIVED   66U
#define XX_WINZIP_AES_PBKDF2_ROUNDS 1000U

typedef struct xx_sha1_context {
    uint32_t state[5];
    uint64_t total_size;
    uint8_t buffer[XX_SHA1_BLOCK_SIZE];
    size_t buffer_size;
} xx_sha1_context;

typedef struct xx_sha256_context {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[XX_SHA256_BLOCK_SIZE];
    size_t buffer_size;
} xx_sha256_context;

typedef struct xx_aes_context {
    uint8_t round_keys[XX_AES_MAX_ROUND_KEY_SIZE];
    uint8_t sbox[256];
    uint8_t inverse_sbox[256];
    unsigned int rounds;
} xx_aes_context;

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

static uint32_t xx_rotate_right32(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

static uint32_t xx_load_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void xx_store_be32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void xx_sha256_transform(xx_sha256_context *context,
                                const uint8_t block[XX_SHA256_BLOCK_SIZE]) {
    static const uint32_t constants[64] = {
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
        0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
        0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
        0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
        0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
        0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
        0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
        0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
        0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
        0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
        0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
        0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
        0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
        0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
    };
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned int index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = xx_load_be32(block + ((size_t)index * 4U));
    }
    for (index = 16U; index < 64U; ++index) {
        uint32_t s0 = xx_rotate_right32(words[index - 15U], 7U) ^
                      xx_rotate_right32(words[index - 15U], 18U) ^
                      (words[index - 15U] >> 3U);
        uint32_t s1 = xx_rotate_right32(words[index - 2U], 17U) ^
                      xx_rotate_right32(words[index - 2U], 19U) ^
                      (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (index = 0U; index < 64U; ++index) {
        uint32_t upper = xx_rotate_right32(e, 6U) ^
                         xx_rotate_right32(e, 11U) ^
                         xx_rotate_right32(e, 25U);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + upper + choose + constants[index] + words[index];
        uint32_t lower = xx_rotate_right32(a, 2U) ^
                         xx_rotate_right32(a, 13U) ^
                         xx_rotate_right32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = lower + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
    xx_crypto_clear(words, sizeof(words));
}

static void xx_sha256_init(xx_sha256_context *context) {
    static const uint32_t initial[8] = {
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U
    };
    xx_bytes_zero((uint8_t *)context, sizeof(*context));
    xx_bytes_copy((uint8_t *)context->state, (const uint8_t *)initial,
                  sizeof(initial));
}

static void xx_sha256_update(xx_sha256_context *context,
                             const uint8_t *data, size_t size) {
    context->total_size += size;
    while (size > 0U) {
        size_t count = XX_SHA256_BLOCK_SIZE - context->buffer_size;
        if (count > size) count = size;
        xx_bytes_copy(context->buffer + context->buffer_size, data, count);
        context->buffer_size += count;
        data += count;
        size -= count;
        if (context->buffer_size == XX_SHA256_BLOCK_SIZE) {
            xx_sha256_transform(context, context->buffer);
            context->buffer_size = 0U;
        }
    }
}

static void xx_sha256_final(xx_sha256_context *context,
                            uint8_t digest[XX_SHA256_DIGEST_SIZE]) {
    uint64_t bit_size = context->total_size * 8U;
    size_t index = context->buffer_size;
    unsigned int word;
    context->buffer[index++] = 0x80U;
    if (index > 56U) {
        xx_bytes_zero(context->buffer + index, XX_SHA256_BLOCK_SIZE - index);
        xx_sha256_transform(context, context->buffer);
        index = 0U;
    }
    xx_bytes_zero(context->buffer + index, 56U - index);
    for (word = 0U; word < 8U; ++word) {
        context->buffer[63U - word] = (uint8_t)(bit_size >> (8U * word));
    }
    xx_sha256_transform(context, context->buffer);
    for (word = 0U; word < 8U; ++word) {
        xx_store_be32(digest + ((size_t)word * 4U), context->state[word]);
    }
}

static void xx_sha1_transform(xx_sha1_context *context, const uint8_t block[XX_SHA1_BLOCK_SIZE]) {
    uint32_t words[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    unsigned int index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = xx_load_be32(block + ((size_t)index * 4U));
    }
    for (index = 16U; index < 80U; ++index) {
        words[index] = xx_rotate_left32(words[index - 3U] ^ words[index - 8U] ^
                                        words[index - 14U] ^ words[index - 16U], 1U);
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];

    for (index = 0U; index < 80U; ++index) {
        uint32_t function;
        uint32_t constant;
        uint32_t temporary;

        if (index < 20U) {
            function = (b & c) | ((~b) & d);
            constant = 0x5A827999U;
        } else if (index < 40U) {
            function = b ^ c ^ d;
            constant = 0x6ED9EBA1U;
        } else if (index < 60U) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDCU;
        } else {
            function = b ^ c ^ d;
            constant = 0xCA62C1D6U;
        }

        temporary = xx_rotate_left32(a, 5U) + function + e + constant + words[index];
        e = d;
        d = c;
        c = xx_rotate_left32(b, 30U);
        b = a;
        a = temporary;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    xx_crypto_clear(words, sizeof(words));
}

static void xx_sha1_init(xx_sha1_context *context) {
    context->state[0] = 0x67452301U;
    context->state[1] = 0xEFCDAB89U;
    context->state[2] = 0x98BADCFEU;
    context->state[3] = 0x10325476U;
    context->state[4] = 0xC3D2E1F0U;
    context->total_size = 0U;
    context->buffer_size = 0U;
    xx_bytes_zero(context->buffer, sizeof(context->buffer));
}

static void xx_sha1_update(xx_sha1_context *context, const uint8_t *data, size_t data_size) {
    size_t consumed = 0U;

    context->total_size += (uint64_t)data_size;

    if (context->buffer_size > 0U) {
        size_t available = XX_SHA1_BLOCK_SIZE - context->buffer_size;
        size_t amount = data_size < available ? data_size : available;
        xx_bytes_copy(context->buffer + context->buffer_size, data, amount);
        context->buffer_size += amount;
        consumed += amount;
        if (context->buffer_size == XX_SHA1_BLOCK_SIZE) {
            xx_sha1_transform(context, context->buffer);
            context->buffer_size = 0U;
        }
    }

    while (data_size - consumed >= XX_SHA1_BLOCK_SIZE) {
        xx_sha1_transform(context, data + consumed);
        consumed += XX_SHA1_BLOCK_SIZE;
    }

    if (consumed < data_size) {
        size_t remainder = data_size - consumed;
        xx_bytes_copy(context->buffer, data + consumed, remainder);
        context->buffer_size = remainder;
    }
}

static void xx_sha1_final(xx_sha1_context *context, uint8_t digest[XX_SHA1_DIGEST_SIZE]) {
    uint64_t bit_size = context->total_size << 3U;
    unsigned int index;

    context->buffer[context->buffer_size++] = 0x80U;
    if (context->buffer_size > 56U) {
        while (context->buffer_size < XX_SHA1_BLOCK_SIZE) {
            context->buffer[context->buffer_size++] = 0U;
        }
        xx_sha1_transform(context, context->buffer);
        context->buffer_size = 0U;
    }
    while (context->buffer_size < 56U) {
        context->buffer[context->buffer_size++] = 0U;
    }

    for (index = 0U; index < 8U; ++index) {
        context->buffer[56U + index] = (uint8_t)(bit_size >> (56U - (index * 8U)));
    }
    xx_sha1_transform(context, context->buffer);

    for (index = 0U; index < 5U; ++index) {
        xx_store_be32(digest + ((size_t)index * 4U), context->state[index]);
    }
    xx_crypto_clear(context, sizeof(*context));
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
        xx_sha1_final(&context, key_block);
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
    xx_sha1_final(&context, inner_digest);

    xx_sha1_init(&context);
    xx_sha1_update(&context, outer_pad, sizeof(outer_pad));
    xx_sha1_update(&context, inner_digest, sizeof(inner_digest));
    xx_sha1_final(&context, digest);
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

static bool xx_aes_set_key(xx_aes_context *context,
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

static void xx_aes_encrypt_block(const xx_aes_context *context,
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
        xx_aes_encrypt_block(context, counter, key_stream);
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
        !xx_aes_set_key(&aes_context, derived, key_size)) {
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

    if (!xx_aes_set_key(&aes_context, derived, key_size)) {
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

static bool xx_7zip_aes_derive_key(const uint8_t *password,
                                   size_t password_size,
                                   const uint8_t *salt,
                                   size_t salt_size,
                                   uint8_t cycles_power,
                                   uint8_t key[XX_SHA256_DIGEST_SIZE]) {
    xx_sha256_context hash;
    uint8_t counter[8];
    uint32_t rounds;
    uint32_t round;

    if ((password_size > 0U && !password) || (salt_size > 0U && !salt)) {
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
        for (index = 0U; index < 4U; ++index) {
            counter[index] = (uint8_t)(round >> (8U * index));
        }
        if (salt_size > 0U) xx_sha256_update(&hash, salt, salt_size);
        if (password_size > 0U) xx_sha256_update(&hash, password, password_size);
        xx_sha256_update(&hash, counter, sizeof(counter));
    }
    xx_sha256_final(&hash, key);
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
                                 salt, salt_size, cycles_power, key) ||
        !xx_aes_set_key(&aes_context, key, sizeof(key))) {
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
    if (!xx_aes_set_key(&aes, key, key_size)) {
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
            xx_sha1_final(&snapshot, digest);
            iv[round >> 14] = digest[19];
        }
    }
    if (xx_pd_is_stopped(pd)) goto cleanup;
    xx_sha1_final(&hash, digest);
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
        xx_sha256_final(&hash, material);
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
    xx_sha256_final(&hash, inner);
    hash = base->outer;
    xx_sha256_update(&hash, inner, sizeof(inner));
    xx_sha256_final(&hash, digest);
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
    xx_sha256_final(&hash, checksum);
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
