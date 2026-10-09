/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RenolY2/j3dview/master/j3d/model.py
 * J3D2 bmd3 models with exactly the eight ordered INF1/VTX1/EVP1/DRW1/JNT1/SHP1/MAT3/TEX1 sections,32-byte aligned lengths. Exports each framed encoded section; nested GPU/geometry structures are preserved without rendering or pointer interpretation. Other J3D revisions and BDL extensions unsupported.
 */
#ifndef XX_NINTENDO_J3D_BMD_H
#define XX_NINTENDO_J3D_BMD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_j3d_bmd { Abstractformat format; } xx_nintendo_j3d_bmd;
XXFC_API void xx_nintendo_j3d_bmd_init(xx_nintendo_j3d_bmd *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_j3d_bmd *xx_nintendo_j3d_bmd_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_j3d_bmd_destroy(xx_nintendo_j3d_bmd *);
XXFC_API void xx_nintendo_j3d_bmd_free(xx_nintendo_j3d_bmd *);
XXFC_API bool xx_nintendo_j3d_bmd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_j3d_bmd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_j3d_bmd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_j3d_bmd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_j3d_bmd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_j3d_bmd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_j3d_bmd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
