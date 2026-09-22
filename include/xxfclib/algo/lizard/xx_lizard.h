/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_LIZARD_H
#define XX_LIZARD_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decodes one or more Lizard frames into an exact-size memory buffer. */
XXFC_API bool xx_lizard_decompress_memory(const void *source, size_t source_size,
                                          void *destination,
                                          size_t destination_size,
                                          size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* XX_LIZARD_H */
