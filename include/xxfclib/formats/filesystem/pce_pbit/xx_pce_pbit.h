/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_PBIT_H
#define XX_PCE_PBIT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE PBIT v0 bitstream disk images, the predecessor of PRI. */
typedef struct xx_pce_pbit_s {
    Abstractformat format;
} xx_pce_pbit;

XXFC_API void xx_pce_pbit_init(xx_pce_pbit *, xx_io_device *, int64_t);
XXFC_API xx_pce_pbit *xx_pce_pbit_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_pbit_destroy(xx_pce_pbit *);
XXFC_API void xx_pce_pbit_free(xx_pce_pbit *);

#ifdef __cplusplus
}
#endif
#endif
