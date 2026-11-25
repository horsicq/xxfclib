/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_PFI_H
#define XX_PCE_PFI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE PFI version-0 flux images. Each track exposes its clock rate, index
 * positions and decoded pulse intervals as little-endian uint32 streams. */
typedef struct xx_pce_pfi_s {
    Abstractformat format;
} xx_pce_pfi;

XXFC_API void xx_pce_pfi_init(xx_pce_pfi *, xx_io_device *, int64_t);
XXFC_API xx_pce_pfi *xx_pce_pfi_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_pfi_destroy(xx_pce_pfi *);
XXFC_API void xx_pce_pfi_free(xx_pce_pfi *);

#ifdef __cplusplus
}
#endif
#endif
