/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://geomview.sourceforge.net/docs/html/OFF.html
 * ASCII OFF polygon meshes with complete counts, finite three-coordinate vertices, bounded nondegenerate index lists and optional RGB/RGBA face colors. COFF/NOFF/higher-dimensional and binary dialects unsupported. Original encoded typed sections exported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_OFF_MESH_H
#define XX_OFF_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_off_mesh {Abstractformat format;} xx_off_mesh;
XXFC_API void xx_off_mesh_init(xx_off_mesh *,xx_io_device *,int64_t);
XXFC_API xx_off_mesh *xx_off_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_off_mesh_destroy(xx_off_mesh *);
XXFC_API void xx_off_mesh_free(xx_off_mesh *);
XXFC_API bool xx_off_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_off_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
