/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/Danny02/OpenCTM/master/lib/compressRAW.c
 * OpenCTM version5 RAW meshes with bounded nondegenerate triangle index triplets, finite vertex/normal/UV/attribute arrays, length-framed map names and exact complete section ordering. MG1/MG2 and geometry rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_OPENCTM_MESH_H
#define XX_OPENCTM_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openctm_mesh { Abstractformat format; } xx_openctm_mesh;
XXFC_API void xx_openctm_mesh_init(xx_openctm_mesh *,xx_io_device *,int64_t);
XXFC_API xx_openctm_mesh *xx_openctm_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_openctm_mesh_destroy(xx_openctm_mesh *);
XXFC_API void xx_openctm_mesh_free(xx_openctm_mesh *);
XXFC_API bool xx_openctm_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openctm_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
