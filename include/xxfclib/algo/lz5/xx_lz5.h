/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_LZ5_H
#define XX_LZ5_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decodes one or more standard LZ5 frames into an exact-size buffer. */
XXFC_API bool xx_lz5_decompress_memory(const void *source, size_t source_size,
                                       void *destination, size_t destination_size,
                                       size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* XX_LZ5_H */
