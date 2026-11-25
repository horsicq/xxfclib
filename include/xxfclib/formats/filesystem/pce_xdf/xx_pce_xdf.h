/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_XDF_H
#define XX_PCE_XDF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE/IBM XDF 1.84 MB physical sector layout. Explicit selection only: the
 * image has no signature and only its fixed extent identifies the layout. */
typedef struct xx_pce_xdf_s {
    Abstractformat format;
} xx_pce_xdf;

XXFC_API void xx_pce_xdf_init(xx_pce_xdf *, xx_io_device *, int64_t);
XXFC_API xx_pce_xdf *xx_pce_xdf_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_xdf_destroy(xx_pce_xdf *);
XXFC_API void xx_pce_xdf_free(xx_pce_xdf *);

#ifdef __cplusplus
}
#endif
#endif
