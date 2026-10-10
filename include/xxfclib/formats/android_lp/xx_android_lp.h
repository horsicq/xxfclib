/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ANDROID_LP_READER_H
#define XX_ANDROID_LP_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_android_lp;
XXFC_API void xx_android_lp_init(xx_android_lp *, xx_io_device *, int64_t);
XXFC_API xx_android_lp *xx_android_lp_create(xx_io_device *, int64_t);
XXFC_API void xx_android_lp_destroy(xx_android_lp *);
XXFC_API void xx_android_lp_free(xx_android_lp *);
static inline Abstractformat *xx_android_lp_to_format(xx_android_lp *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
