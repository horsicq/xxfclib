/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/modelgen.h
 * Quake alias model IDPO version6, single skins and single animation frames with up to64 skins/4096 vertices/65536 triangles/1024 frames. Validates geometry indices, finite header/texture coordinates and normal indices. Exports skin planes, texcoords, triangles and encoded frame data; grouped skins/frames, rendering and external palettes unsupported.
 */
#ifndef XX_IDTECH_MDL_H
#define XX_IDTECH_MDL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_idtech_mdl { Abstractformat format; } xx_idtech_mdl;
XXFC_API void xx_idtech_mdl_init(xx_idtech_mdl *,xx_io_device *,int64_t);
XXFC_API xx_idtech_mdl *xx_idtech_mdl_create(xx_io_device *,int64_t);
XXFC_API void xx_idtech_mdl_destroy(xx_idtech_mdl *);
XXFC_API void xx_idtech_mdl_free(xx_idtech_mdl *);
XXFC_API bool xx_idtech_mdl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_idtech_mdl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_idtech_mdl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_idtech_mdl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_idtech_mdl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_idtech_mdl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_idtech_mdl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
