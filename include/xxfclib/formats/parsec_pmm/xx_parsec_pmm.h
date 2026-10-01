/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PARSEC_PMM_H
#define XX_PARSEC_PMM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MTCVTS PSM 2.00 music module with MDH/PLX/SM8 components. */
typedef struct xx_parsec_pmm { Abstractformat format; } xx_parsec_pmm;

XXFC_API void xx_parsec_pmm_init(xx_parsec_pmm *, xx_io_device *, int64_t);
XXFC_API xx_parsec_pmm *xx_parsec_pmm_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_pmm_destroy(xx_parsec_pmm *);
XXFC_API void xx_parsec_pmm_free(xx_parsec_pmm *);

#ifdef __cplusplus
}
#endif
#endif
