/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ARCV2_LZHUF_H
#define XXFCLIB_ALGO_ARCV2_LZHUF_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compact/wide Yoshizaki LZHUF streams used by Eschalon Setup ARCV.
 * ARCV v2 uses the compact variant (maximum match length 32); the wide
 * switch is exposed because both variants share the exact bitstream model. */
XXFC_API bool xx_arcv2_lzhuf_decode_memory(const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_size, bool wide,
                                           size_t *written);

/* ARCV v2 writers optionally apply this prefix-XOR filter to every packed
 * member.  The operation is safe in place. */
XXFC_API bool xx_arcv2_xor_delta_decode(uint8_t *data, size_t size,
                                        uint8_t seed);

#ifdef __cplusplus
}
#endif

#endif
