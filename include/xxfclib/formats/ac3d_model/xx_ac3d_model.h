/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.inivis.com/ac3d/man/ac3dfileformat.html
 * AC3Db ASCII material palette and complete bounded world/group/poly hierarchy, finite transforms/vertices, surface flags/material references and complete indexed UV
 * records. Unknown fields, lights, subdivision and external resources unsupported. Original typed sections remain encoded. Limits64MiB input,4096 components; encoded
 * assets are never executed.
 */
#ifndef XX_AC3D_MODEL_H
#define XX_AC3D_MODEL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ac3d_model {
    Abstractformat format;
} xx_ac3d_model;
XXFC_API void xx_ac3d_model_init(xx_ac3d_model *, xx_io_device *, int64_t);
XXFC_API xx_ac3d_model *xx_ac3d_model_create(xx_io_device *, int64_t);
XXFC_API void xx_ac3d_model_destroy(xx_ac3d_model *);
XXFC_API void xx_ac3d_model_free(xx_ac3d_model *);
XXFC_API bool xx_ac3d_model_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ac3d_model_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ac3d_model_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ac3d_model_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ac3d_model_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ac3d_model_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ac3d_model_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
