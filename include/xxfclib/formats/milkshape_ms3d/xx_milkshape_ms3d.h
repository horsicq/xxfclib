/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/MS3D/MS3DLoader.cpp
 * MilkShape3D version4 base static models without joints or optional extension blocks, up to8192 vertices/16384 triangles/128 groups/materials. Validates finite geometry/materials and vertex/triangle/group/material references. Exports geometry/group/material tables; skeletons/extra comments/weights, texture loading and rendering unsupported.
 */
#ifndef XX_MILKSHAPE_MS3D_H
#define XX_MILKSHAPE_MS3D_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_milkshape_ms3d { Abstractformat format; } xx_milkshape_ms3d;
XXFC_API void xx_milkshape_ms3d_init(xx_milkshape_ms3d *,xx_io_device *,int64_t);
XXFC_API xx_milkshape_ms3d *xx_milkshape_ms3d_create(xx_io_device *,int64_t);
XXFC_API void xx_milkshape_ms3d_destroy(xx_milkshape_ms3d *);
XXFC_API void xx_milkshape_ms3d_free(xx_milkshape_ms3d *);
XXFC_API bool xx_milkshape_ms3d_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_milkshape_ms3d_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_milkshape_ms3d_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_milkshape_ms3d_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_milkshape_ms3d_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
