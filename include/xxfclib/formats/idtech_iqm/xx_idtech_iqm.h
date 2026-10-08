/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/lsalzman/iqm/master/iqm.h
 * IQM2 static meshes with text/mesh/triangle tables and float position/texcoord/normal/tangent or byte-color arrays, up to65536 vertices/triangles and256 meshes. Validates mesh/index/string references, finite values, disjoint buffers and disjoint mesh triangle groups covering all triangles. Exports stored encoded tables/arrays; joints, poses, animation, blend attributes, custom arrays/extensions and rendering unsupported.
 */
#ifndef XX_IDTECH_IQM_H
#define XX_IDTECH_IQM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_idtech_iqm { Abstractformat format; } xx_idtech_iqm;
XXFC_API void xx_idtech_iqm_init(xx_idtech_iqm *,xx_io_device *,int64_t);
XXFC_API xx_idtech_iqm *xx_idtech_iqm_create(xx_io_device *,int64_t);
XXFC_API void xx_idtech_iqm_destroy(xx_idtech_iqm *);
XXFC_API void xx_idtech_iqm_free(xx_idtech_iqm *);
XXFC_API bool xx_idtech_iqm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_idtech_iqm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_idtech_iqm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_idtech_iqm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_idtech_iqm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
