/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/DOOM-3/master/neo/renderer/Model_md5.cpp
 * MD5Version10 skeletal meshes, strict complete text grammar with quoted names and // comments. Validates parent hierarchy, finite transforms/UV/weights, quaternion XYZ norm, sequential indices, vertex/triangle/joint references and normalized influence sums. Up to256 joints/meshes,65536 vertices/triangles/weights per mesh;64MiB text. Exports original joints and mesh text components; escaped/non-ASCII names, MD5 animations and rendering unsupported.
 */
#ifndef XX_QUAKE_MD5MESH_H
#define XX_QUAKE_MD5MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_quake_md5mesh { Abstractformat format; } xx_quake_md5mesh;
XXFC_API void xx_quake_md5mesh_init(xx_quake_md5mesh *,xx_io_device *,int64_t);
XXFC_API xx_quake_md5mesh *xx_quake_md5mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_quake_md5mesh_destroy(xx_quake_md5mesh *);
XXFC_API void xx_quake_md5mesh_free(xx_quake_md5mesh *);
XXFC_API bool xx_quake_md5mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_quake_md5mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_quake_md5mesh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_quake_md5mesh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_quake_md5mesh_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
