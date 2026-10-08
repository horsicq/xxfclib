/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/B3D/B3DImporter.cpp
 * BB3D version1 static model hierarchies, up to1024 nodes/depth32, bounded texture/brush records and finite vertex/triangle streams. Exports encoded texture/brush/mesh streams and validates bindings/indices. Bones/keyframes/animation/unknown chunks, external texture loading and rendering unsupported.
 */
#ifndef XX_BLITZ3D_B3D_H
#define XX_BLITZ3D_B3D_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_blitz3d_b3d { Abstractformat format; } xx_blitz3d_b3d;
XXFC_API void xx_blitz3d_b3d_init(xx_blitz3d_b3d *,xx_io_device *,int64_t);
XXFC_API xx_blitz3d_b3d *xx_blitz3d_b3d_create(xx_io_device *,int64_t);
XXFC_API void xx_blitz3d_b3d_destroy(xx_blitz3d_b3d *);
XXFC_API void xx_blitz3d_b3d_free(xx_blitz3d_b3d *);
XXFC_API bool xx_blitz3d_b3d_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_blitz3d_b3d_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_blitz3d_b3d_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_blitz3d_b3d_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_blitz3d_b3d_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
