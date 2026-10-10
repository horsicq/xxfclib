/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_UNIX_V7_READER_H
#define XX_UNIX_V7_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_unix_v7;
XXFC_API void xx_unix_v7_init(xx_unix_v7 *, xx_io_device *, int64_t);
XXFC_API xx_unix_v7 *xx_unix_v7_create(xx_io_device *, int64_t);
XXFC_API void xx_unix_v7_destroy(xx_unix_v7 *);
XXFC_API void xx_unix_v7_free(xx_unix_v7 *);
static inline Abstractformat *xx_unix_v7_to_format(xx_unix_v7 *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
