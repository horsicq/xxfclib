/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://docs.imgtec.com/specifications/pvr-file-format-specification/html/topics/pvr-header-format.html
 * PVR3 both byte orders: byte-aligned channel formats and selected ETC/BC block formats, metadata and encoded mip groups. PVRTC/ASTC/Basis and pixel decoding are not supported.
 */
#ifndef XX_PVR_H
#define XX_PVR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pvr { Abstractformat format; } xx_pvr;
XXFC_API void xx_pvr_init(xx_pvr *,xx_io_device *,int64_t);
XXFC_API xx_pvr *xx_pvr_create(xx_io_device *,int64_t);
XXFC_API void xx_pvr_destroy(xx_pvr *);
XXFC_API void xx_pvr_free(xx_pvr *);
XXFC_API bool xx_pvr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pvr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
