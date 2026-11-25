/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCE_TC_H
#define XX_PCE_TC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TransCopy track-bitstream disk images via the PCE TC layout. */
typedef struct xx_pce_tc_s {
    Abstractformat format;
} xx_pce_tc;

XXFC_API void xx_pce_tc_init(xx_pce_tc *, xx_io_device *, int64_t);
XXFC_API xx_pce_tc *xx_pce_tc_create(xx_io_device *, int64_t);
XXFC_API void xx_pce_tc_destroy(xx_pce_tc *);
XXFC_API void xx_pce_tc_free(xx_pce_tc *);

#ifdef __cplusplus
}
#endif
#endif
