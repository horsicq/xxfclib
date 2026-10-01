/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide
 * DDS encoded mip surfaces: tightly packed RGB/luminance, DXT1/3/5, BC4/5 and selected DX10 BC/RGBA formats. No pixel decoding; unsupported formats or incomplete cube faces are rejected.
 */
#ifndef XX_DDS_H
#define XX_DDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dds { Abstractformat format; } xx_dds;
XXFC_API void xx_dds_init(xx_dds *,xx_io_device *,int64_t);
XXFC_API xx_dds *xx_dds_create(xx_io_device *,int64_t);
XXFC_API void xx_dds_destroy(xx_dds *);
XXFC_API void xx_dds_free(xx_dds *);
XXFC_API bool xx_dds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dds_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
