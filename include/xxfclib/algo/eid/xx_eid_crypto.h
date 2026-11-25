/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_EID_CRYPTO_H
#define XX_EID_CRYPTO_H
#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_EID_KEY_SIZE 32U
#define XX_EID_KEY_SLOT_SIZE 64U

/** EID's legacy Rijndael key expansion and ciphertext-XOR feedback mode.
 * These are complete independent messages, not streaming updates: feedback
 * resets for each call. Any size, including a partial final block, is valid.
 * Output holds input_size bytes; exact in-place operation is supported.
 * Other input/output overlaps are rejected. Key/output may overlap.
 * NULL input/output is valid only for size zero; key is always required.
 * Invalid arguments leave output unchanged. No authentication is performed:
 * validate directory grammar/checksums after decryption. This cipher is
 * incompatible with standard AES-256 and is for existing EID data.
 */
XXFC_API bool xx_eid_rijndael_encrypt(const uint8_t *input, size_t input_size, const uint8_t key32[32], uint8_t *output);
XXFC_API bool xx_eid_rijndael_decrypt(const uint8_t *input, size_t input_size, const uint8_t key32[32], uint8_t *output);

/** Decrypt exactly one 64-byte key slot with the supplied 32-byte master key,
 * then hash all 64 decrypted bytes with RIPEMD-256 to produce the archive key.
 * No master-key search or validation is performed. Inputs must have the stated
 * sizes. Output may alias either input. All pointers are required; failure
 * clears output when supplied. Temporary plaintext/key material is cleared.
 */
XXFC_API bool xx_eid_derive_key(const uint8_t master_key32[32], const uint8_t encrypted_slot64[64], uint8_t key32[32]);

#ifdef __cplusplus
}
#endif
#endif
