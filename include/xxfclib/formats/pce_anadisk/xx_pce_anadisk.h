/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_ANADISK_H
#define XX_PCE_ANADISK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AnaDisk sequential sector records, validated without a file magic. */
typedef struct xx_pce_anadisk_s {
    Abstractformat format;
    bool conservative_probe;
} xx_pce_anadisk;

XXFC_API void xx_pce_anadisk_init(xx_pce_anadisk *, xx_io_device *, int64_t);
XXFC_API xx_pce_anadisk *xx_pce_anadisk_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_anadisk_destroy(xx_pce_anadisk *);
XXFC_API void xx_pce_anadisk_free(xx_pce_anadisk *);
/* Auto-probing an unsignatured stream requires at least two nonempty records.
 * Explicitly selected readers retain support for one or empty sectors. */
XXFC_API void xx_pce_anadisk_set_conservative_probe(xx_pce_anadisk *, bool);

#ifdef __cplusplus
}
#endif
#endif
