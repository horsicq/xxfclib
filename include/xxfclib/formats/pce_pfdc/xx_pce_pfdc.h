/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_PFDC_H
#define XX_PCE_PFDC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCE PFDC physical-sector disk images. Version is 0, 1, 2 or 4. */
typedef struct xx_pce_pfdc_s {
    Abstractformat format;
    unsigned version;
} xx_pce_pfdc;

XXFC_API void xx_pce_pfdc_init(xx_pce_pfdc *, xx_io_device *, int64_t,
                               unsigned version);
XXFC_API xx_pce_pfdc *xx_pce_pfdc_create(xx_io_device *, int64_t,
                                         unsigned version);
XXFC_API void xx_pce_pfdc_destroy(xx_pce_pfdc *);
XXFC_API void xx_pce_pfdc_free(xx_pce_pfdc *);

#ifdef __cplusplus
}
#endif
#endif
