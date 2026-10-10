/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/nickworonekin/puyotools/master/src/PuyoTools.Core/Textures/Gvr/GvrTextureDecoder.cs
 * Direct GVRT chunk with single-level tiled I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8 orCMPR plane; validates tile-rounded encoded size. Exports encoded texture bytes;
 * GBIX/GCIX, palettes, mipmaps, PRS, pixel conversion and rendering unsupported.
 */
#ifndef XX_SEGA_GVR_H
#define XX_SEGA_GVR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_gvr {
    Abstractformat format;
} xx_sega_gvr;
XXFC_API void xx_sega_gvr_init(xx_sega_gvr *, xx_io_device *, int64_t);
XXFC_API xx_sega_gvr *xx_sega_gvr_create(xx_io_device *, int64_t);
XXFC_API void xx_sega_gvr_destroy(xx_sega_gvr *);
XXFC_API void xx_sega_gvr_free(xx_sega_gvr *);
XXFC_API bool xx_sega_gvr_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sega_gvr_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sega_gvr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sega_gvr_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sega_gvr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sega_gvr_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sega_gvr_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
