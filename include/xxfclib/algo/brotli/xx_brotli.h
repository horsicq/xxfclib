/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_BROTLI_H
#define XX_BROTLI_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decodes one complete Brotli stream into a bounded memory buffer.
 *
 * In addition to RFC 7932 streams, the decoder accepts the independent-frame
 * wrapper used by the Brotli 7-Zip codec. The destination size is exact: a
 * successful call always writes exactly destination_size bytes.
 */
XXFC_API bool xx_brotli_decompress_memory(const void *source,
                                          size_t source_size,
                                          void *destination,
                                          size_t destination_size,
                                          size_t *out_written);

/**
 * Decodes a complete Brotli stream into a bounded, library-allocated buffer.
 * The caller owns `*out_data` and releases it with xx_mem_free().  This is
 * useful for raw Brotli streams, which do not carry a universal expanded-size
 * field.
 */
XXFC_API bool xx_brotli_decompress_alloc(const void *source,
                                         size_t source_size,
                                         uint8_t **out_data,
                                         size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* XX_BROTLI_H */
