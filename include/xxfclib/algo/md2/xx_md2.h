/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_MD2_H
#define XX_MD2_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_MD2_BLOCK_SIZE 16U
#define XX_MD2_DIGEST_SIZE 16U

typedef struct xx_md2_context {
    uint8_t state[48];
    uint8_t checksum[16];
    uint8_t buffer[16];
    size_t buffer_size;
    bool initialized;
} xx_md2_context;

/* Adapted from xxyara src/yx_md2.h. Contexts may be copied. NULL input is valid
 * for size zero. Failed calls leave initialized contexts unchanged. A successful
 * final consumes and clears the context; output must hold at least 16 bytes. */
XXFC_API bool xx_md2_init(xx_md2_context *context);
XXFC_API bool xx_md2_update(xx_md2_context *context, const void *data, size_t size);
XXFC_API bool xx_md2_final(xx_md2_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_md2_memory(const void *data, size_t size, void *digest);

#ifdef __cplusplus
}
#endif
#endif
