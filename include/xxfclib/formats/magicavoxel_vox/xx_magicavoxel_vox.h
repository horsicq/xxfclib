/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ephtracy/voxel-model/master/MagicaVoxel-file-format-vox.txt
 * MagicaVoxel VOX150 flat MAIN with SIZE/XYZI model pairs, optional PACK and RGBA, up to256 models/256-cubed dimensions/1million voxels per model. Checks chunk extents, voxel coordinates/colors and duplicate occupancy. Exports stored size/voxel/palette components; scene/material/transform extensions, other revisions and rendering unsupported.
 */
#ifndef XX_MAGICAVOXEL_VOX_H
#define XX_MAGICAVOXEL_VOX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_magicavoxel_vox { Abstractformat format; } xx_magicavoxel_vox;
XXFC_API void xx_magicavoxel_vox_init(xx_magicavoxel_vox *,xx_io_device *,int64_t);
XXFC_API xx_magicavoxel_vox *xx_magicavoxel_vox_create(xx_io_device *,int64_t);
XXFC_API void xx_magicavoxel_vox_destroy(xx_magicavoxel_vox *);
XXFC_API void xx_magicavoxel_vox_free(xx_magicavoxel_vox *);
XXFC_API bool xx_magicavoxel_vox_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_magicavoxel_vox_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_magicavoxel_vox_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_magicavoxel_vox_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_magicavoxel_vox_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
