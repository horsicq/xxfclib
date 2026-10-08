/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/nickworonekin/puyotools/master/src/PuyoTools.Core/Textures/Pvr/PvrTextureEncoder.cs
 * Dreamcast PVRT container, direct 16-bit ARGB1555/RGB565/ARGB4444 texture data, rectangular or square/rectangular twiddled single level. Encoded bytes exported; GBIX wrapper, VQ/palette/mipmaps, RLE and pixel rendering unsupported. Distinct from PVR3.
 */
#ifndef XX_SEGA_PVR2_H
#define XX_SEGA_PVR2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_pvr2 { Abstractformat format; } xx_sega_pvr2;
XXFC_API void xx_sega_pvr2_init(xx_sega_pvr2 *,xx_io_device *,int64_t);
XXFC_API xx_sega_pvr2 *xx_sega_pvr2_create(xx_io_device *,int64_t);
XXFC_API void xx_sega_pvr2_destroy(xx_sega_pvr2 *);
XXFC_API void xx_sega_pvr2_free(xx_sega_pvr2 *);
XXFC_API bool xx_sega_pvr2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sega_pvr2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sega_pvr2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sega_pvr2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sega_pvr2_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
