/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RenolY2/j3dview/master/j3d/ttk1.py
 * J3D1 btk1 with one TTK1 texture-matrix animation section, up to256 matrices and constant/spline scalar selections. Checks all table extents, finite float values, string/texture indices and selection ranges. Exports encoded animation/table components; interpolation, rendering and other animation revisions unsupported.
 */
#ifndef XX_NINTENDO_J3D_BTK_H
#define XX_NINTENDO_J3D_BTK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_j3d_btk { Abstractformat format; } xx_nintendo_j3d_btk;
XXFC_API void xx_nintendo_j3d_btk_init(xx_nintendo_j3d_btk *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_j3d_btk *xx_nintendo_j3d_btk_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_j3d_btk_destroy(xx_nintendo_j3d_btk *);
XXFC_API void xx_nintendo_j3d_btk_free(xx_nintendo_j3d_btk *);
XXFC_API bool xx_nintendo_j3d_btk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_j3d_btk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_j3d_btk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_j3d_btk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_j3d_btk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_j3d_btk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_j3d_btk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
