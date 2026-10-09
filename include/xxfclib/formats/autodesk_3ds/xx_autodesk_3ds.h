/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/3DS/3DSHelper.h
 * 3DS static triangular mesh subset: main/version/edit/mesh-version/object chunks, vertex/face tables, optional UV and local matrix. Full nesting extents, finite values, vertex references and unique required chunks; up to1024 objects. Exports original object chunks. Materials, face subchunks, lights, cameras, animation and rendering unsupported.
 */
#ifndef XX_AUTODESK_3DS_H
#define XX_AUTODESK_3DS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autodesk_3ds { Abstractformat format; } xx_autodesk_3ds;
XXFC_API void xx_autodesk_3ds_init(xx_autodesk_3ds *,xx_io_device *,int64_t);
XXFC_API xx_autodesk_3ds *xx_autodesk_3ds_create(xx_io_device *,int64_t);
XXFC_API void xx_autodesk_3ds_destroy(xx_autodesk_3ds *);
XXFC_API void xx_autodesk_3ds_free(xx_autodesk_3ds *);
XXFC_API bool xx_autodesk_3ds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_autodesk_3ds_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_autodesk_3ds_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_autodesk_3ds_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_autodesk_3ds_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_autodesk_3ds_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_autodesk_3ds_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
