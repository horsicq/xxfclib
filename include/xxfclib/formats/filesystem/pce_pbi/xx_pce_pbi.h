/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_PBI_H
#define XX_PCE_PBI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE Block Image version 0. One member is the reconstructed disk byte stream. */
typedef struct xx_pce_pbi_s {
    Abstractformat format;
} xx_pce_pbi;

XXFC_API void xx_pce_pbi_init(xx_pce_pbi *, xx_io_device *, int64_t);
XXFC_API xx_pce_pbi *xx_pce_pbi_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_pbi_destroy(xx_pce_pbi *);
XXFC_API void xx_pce_pbi_free(xx_pce_pbi *);

#ifdef __cplusplus
}
#endif
#endif
