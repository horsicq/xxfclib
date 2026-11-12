/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DEMONSTAR_GLB_H
#define XXFCLIB_FORMAT_DEMONSTAR_GLB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Demonstar GLB 2.0: "GLB2.0" magic, u32 count, 28-byte stored entries
 * (flags/offset/size/name[16]); the plain form of the Raptor GLB table. */
XXFC_API Abstractformat *xx_demonstar_glb_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_demonstar_glb_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_demonstar_glb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_demonstar_glb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_demonstar_glb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_demonstar_glb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_demonstar_glb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
