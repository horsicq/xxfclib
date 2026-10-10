/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PARSEC_DTC_H
#define XX_PARSEC_DTC_H
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_legacy_sound_driver xx_parsec_dtc;
XXFC_API void xx_parsec_dtc_init(xx_parsec_dtc *,xx_io_device *,int64_t);
XXFC_API xx_parsec_dtc *xx_parsec_dtc_create(xx_io_device *,int64_t);
XXFC_API void xx_parsec_dtc_destroy(xx_parsec_dtc *);
XXFC_API void xx_parsec_dtc_free(xx_parsec_dtc *);
static inline Abstractformat *xx_parsec_dtc_to_format(xx_parsec_dtc *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
