/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/godotengine/godot/master/scene/resources/compressed_texture.cpp
 * Godot4 GST2 version1 raw uncompressed L8/LA8/R8/RG8/RGB8/RGBA8 textures, up to16 complete mip levels and8192-pixel dimensions. Exports each stored mip plane with checked geometry/format length; PNG/WebP/Basis/GPU-compressed modes and rendering unsupported.
 */
#ifndef XX_GODOT_CTEX_H
#define XX_GODOT_CTEX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_godot_ctex { Abstractformat format; } xx_godot_ctex;
XXFC_API void xx_godot_ctex_init(xx_godot_ctex *,xx_io_device *,int64_t);
XXFC_API xx_godot_ctex *xx_godot_ctex_create(xx_io_device *,int64_t);
XXFC_API void xx_godot_ctex_destroy(xx_godot_ctex *);
XXFC_API void xx_godot_ctex_free(xx_godot_ctex *);
XXFC_API bool xx_godot_ctex_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_godot_ctex_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_godot_ctex_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_godot_ctex_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_godot_ctex_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_godot_ctex_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_godot_ctex_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
