/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/eid/xx_eid_crypto.h"
#include "xxfclib/algo/ripemd256/xx_ripemd256.h"
#include "xxfclib/rt/xx_rt.h"
#include "../aes/xx_aes_internal.h"

static void eid_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size-- > 0U) *p++ = 0U;
}

static bool eid_set_key(xx_aes_context *context, const uint8_t key[32])
{
    uint8_t words[32], rcon = 1U;
    size_t generated = 32U;
    unsigned i, j;
    if (!xx_aes_internal_set_key(context, key, 32U)) return false;
    xx_rt_memcpy(words, context->round_keys, 32U);
    while (generated < 240U) {
        uint8_t temp[4] = {context->sbox[words[29]], context->sbox[words[30]], context->sbox[words[31]], context->sbox[words[28]]};
        temp[0] ^= rcon;
        rcon = (uint8_t)((rcon << 1U) ^ ((rcon & 0x80U) ? 0x1bU : 0U));
        for (j = 0; j < 4U; ++j) words[j] ^= temp[j];
        /* The first chain deliberately includes words 4..7 before the
         * substitution and second chain. Standard AES stops at word 3. */
        for (i = 1; i < 8U; ++i)
            for (j = 0; j < 4U; ++j) words[4U * i + j] ^= words[4U * (i - 1U) + j];
        for (j = 0; j < 4U; ++j) words[16U + j] ^= context->sbox[words[12U + j]];
        for (i = 5; i < 8U; ++i)
            for (j = 0; j < 4U; ++j) words[4U * i + j] ^= words[4U * (i - 1U) + j];
        for (j = 0; j < 32U && generated < 240U; ++j) context->round_keys[generated++] = words[j];
        eid_clear(temp, sizeof(temp));
    }
    eid_clear(words, sizeof(words));
    return true;
}

static bool eid_crypt(const uint8_t *input, size_t size, const uint8_t key[32], uint8_t *output, bool encrypt)
{
    xx_aes_context context;
    uint8_t feedback[16], block[16], transformed[16];
    unsigned i;
    uintptr_t in = (uintptr_t)input, out = (uintptr_t)output;
    if (!key || (size && (!input || !output))) return false;
    if (size && input != output && ((in < out && out - in < size) || (out < in && in - out < size))) return false;
    if (!size) return true;
    if (!eid_set_key(&context, key)) return false;
    xx_rt_memset(feedback, 0xff, sizeof(feedback));
    xx_aes_internal_encrypt_block(&context, feedback, feedback);
    while (size >= 16U) {
        xx_rt_memcpy(block, input, 16U);
        if (encrypt) {
            for (i = 0; i < 16U; ++i) transformed[i] = block[i] ^ feedback[i];
            xx_aes_internal_encrypt_block(&context, transformed, block);
            xx_rt_memcpy(output, block, 16U);
        } else {
            xx_aes_internal_decrypt_block(&context, block, transformed);
            for (i = 0; i < 16U; ++i) output[i] = transformed[i] ^ feedback[i];
        }
        for (i = 0; i < 16U; ++i) feedback[i] ^= block[i];
        input += 16U;
        output += 16U;
        size -= 16U;
    }
    if (size) {
        xx_aes_internal_encrypt_block(&context, feedback, transformed);
        for (i = 0; i < size; ++i) output[i] = input[i] ^ transformed[i];
    }
    eid_clear(&context, sizeof(context));
    eid_clear(feedback, sizeof(feedback));
    eid_clear(block, sizeof(block));
    eid_clear(transformed, sizeof(transformed));
    return true;
}

bool xx_eid_rijndael_encrypt(const uint8_t *input, size_t input_size, const uint8_t key32[32], uint8_t *output)
{
    return eid_crypt(input, input_size, key32, output, true);
}

bool xx_eid_rijndael_decrypt(const uint8_t *input, size_t input_size, const uint8_t key32[32], uint8_t *output)
{
    return eid_crypt(input, input_size, key32, output, false);
}

bool xx_eid_derive_key(const uint8_t master_key32[32], const uint8_t encrypted_slot64[64], uint8_t key32[32])
{
    uint8_t slot[64];
    bool ok = false;
    if (master_key32 && encrypted_slot64 && key32 && xx_eid_rijndael_decrypt(encrypted_slot64, 64U, master_key32, slot))
        ok = xx_ripemd256_memory(slot, sizeof(slot), key32);
    if (!ok && key32) eid_clear(key32, 32U);
    eid_clear(slot, sizeof(slot));
    return ok;
}
