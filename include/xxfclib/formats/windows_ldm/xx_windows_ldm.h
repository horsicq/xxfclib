/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_WINDOWS_LDM_READER_H
#define XX_WINDOWS_LDM_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_windows_ldm;
XXFC_API void xx_windows_ldm_init(xx_windows_ldm *,xx_io_device *,int64_t);
XXFC_API xx_windows_ldm *xx_windows_ldm_create(xx_io_device *,int64_t);
XXFC_API void xx_windows_ldm_destroy(xx_windows_ldm *);
XXFC_API void xx_windows_ldm_free(xx_windows_ldm *);
static inline Abstractformat *xx_windows_ldm_to_format(xx_windows_ldm *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
