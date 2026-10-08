/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/OGRECave/ogre/master/OgreMain/src/OgreMeshSerializerImpl.cpp
 * Ogre MeshSerializer_v1.100 little-endian static meshes with nonshared FLOAT3 position-only geometry,16-bit triangle indices and finite bounds. Exports encoded submesh/geometry metadata, indices, vertex buffers and bounds; shared/multiattribute buffers, skeletons, LOD/edges, older serializer revisions and rendering unsupported.
 */
#ifndef XX_OGRE_MESH_H
#define XX_OGRE_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ogre_mesh { Abstractformat format; } xx_ogre_mesh;
XXFC_API void xx_ogre_mesh_init(xx_ogre_mesh *,xx_io_device *,int64_t);
XXFC_API xx_ogre_mesh *xx_ogre_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_ogre_mesh_destroy(xx_ogre_mesh *);
XXFC_API void xx_ogre_mesh_free(xx_ogre_mesh *);
XXFC_API bool xx_ogre_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ogre_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ogre_mesh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ogre_mesh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ogre_mesh_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
