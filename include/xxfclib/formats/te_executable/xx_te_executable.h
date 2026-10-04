/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_TE_EXECUTABLE_READER_H
#define XX_TE_EXECUTABLE_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_te_executable;
XXFC_API void xx_te_executable_init(xx_te_executable *,xx_io_device *,int64_t);
XXFC_API xx_te_executable *xx_te_executable_create(xx_io_device *,int64_t);
XXFC_API void xx_te_executable_destroy(xx_te_executable *);
XXFC_API void xx_te_executable_free(xx_te_executable *);
static inline Abstractformat *xx_te_executable_to_format(xx_te_executable *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
