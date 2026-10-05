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
 * @file xx_aes_internal.h
 * @brief Private AES primitive shared by archive-specific AES modules.
 *
 * This source-tree interface is not part of the installed public API.
 */
#ifndef XX_AES_INTERNAL_H
#define XX_AES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XX_AES_BLOCK_SIZE         16U
#define XX_AES_MAX_ROUND_KEY_SIZE 240U

typedef struct xx_aes_context {
    uint8_t round_keys[XX_AES_MAX_ROUND_KEY_SIZE];
    uint8_t sbox[256];
    uint8_t inverse_sbox[256];
    unsigned int rounds;
} xx_aes_context;

#ifdef __cplusplus
extern "C" {
#endif

bool xx_aes_internal_set_key(xx_aes_context *context,
                              const uint8_t *key, size_t key_size);
void xx_aes_internal_encrypt_block(const xx_aes_context *context,
                                    const uint8_t input[XX_AES_BLOCK_SIZE],
                                    uint8_t output[XX_AES_BLOCK_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* XX_AES_INTERNAL_H */
