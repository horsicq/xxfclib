/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_PRI_H
#define XX_PCE_PRI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE PRI version-0 bitstream disk images. Each track's original encoded
 * bit cells and optional UTF-8 TEXT chunks are listed and extractable.
 * Every chunk's non-reflected CRC-32C is verified before publication. */
typedef struct xx_pce_pri_s {
    Abstractformat format;
} xx_pce_pri;

XXFC_API void xx_pce_pri_init(xx_pce_pri *, xx_io_device *, int64_t);
XXFC_API xx_pce_pri *xx_pce_pri_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_pri_destroy(xx_pce_pri *);
XXFC_API void xx_pce_pri_free(xx_pce_pri *);

#ifdef __cplusplus
}
#endif
#endif
