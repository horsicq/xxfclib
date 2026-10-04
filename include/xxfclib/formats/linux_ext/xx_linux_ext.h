/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_LINUX_EXT_READER_H
#define XX_LINUX_EXT_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_linux_ext;
XXFC_API void xx_linux_ext_init(xx_linux_ext *,xx_io_device *,int64_t);
XXFC_API xx_linux_ext *xx_linux_ext_create(xx_io_device *,int64_t);
XXFC_API void xx_linux_ext_destroy(xx_linux_ext *);
XXFC_API void xx_linux_ext_free(xx_linux_ext *);
static inline Abstractformat *xx_linux_ext_to_format(xx_linux_ext *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
