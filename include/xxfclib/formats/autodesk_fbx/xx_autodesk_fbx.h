/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/blender/blender-addons/main/io_scene_fbx/encode_bin.py
 * Binary FBX7400/7500, bounded recursive node/property grammar with scalar/string/raw and stored/zlib array properties. Validates finite floats, exact array inflation size and Adler32, child sentinels and standard footer/version. Depth32,65536 nodes,4096 root members,16MiB per array and64MiB cumulative expanded arrays. Exports each original root subtree plus footer; semantic object connections, rendering and ASCII FBX unsupported.
 */
#ifndef XX_AUTODESK_FBX_H
#define XX_AUTODESK_FBX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autodesk_fbx { Abstractformat format; } xx_autodesk_fbx;
XXFC_API void xx_autodesk_fbx_init(xx_autodesk_fbx *,xx_io_device *,int64_t);
XXFC_API xx_autodesk_fbx *xx_autodesk_fbx_create(xx_io_device *,int64_t);
XXFC_API void xx_autodesk_fbx_destroy(xx_autodesk_fbx *);
XXFC_API void xx_autodesk_fbx_free(xx_autodesk_fbx *);
XXFC_API bool xx_autodesk_fbx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_autodesk_fbx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_autodesk_fbx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_autodesk_fbx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_autodesk_fbx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
