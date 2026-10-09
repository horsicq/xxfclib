/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/blender/blender/main/source/blender/io/wavefront_obj/importer/obj_import_file_reader.cc
 * UTF8 OBJ polygon meshes: complete finite vertex/texture/normal records, positive and relative face references, bounded named groups/material metadata. Freeform curves, continuations, external materials and rendering unsupported. Offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_WAVEFRONT_OBJ_H
#define XX_WAVEFRONT_OBJ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wavefront_obj {Abstractformat format;} xx_wavefront_obj;
XXFC_API void xx_wavefront_obj_init(xx_wavefront_obj *,xx_io_device *,int64_t);
XXFC_API xx_wavefront_obj *xx_wavefront_obj_create(xx_io_device *,int64_t);
XXFC_API void xx_wavefront_obj_destroy(xx_wavefront_obj *);
XXFC_API void xx_wavefront_obj_free(xx_wavefront_obj *);
XXFC_API bool xx_wavefront_obj_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wavefront_obj_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_wavefront_obj_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_wavefront_obj_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_wavefront_obj_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_wavefront_obj_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_wavefront_obj_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
