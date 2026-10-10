/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/mikedh/trimesh/main/trimesh/exchange/stl.py
 * Binary STL with exact84+50*facet count framing,1-1000000 finite nondegenerate triangular facets and zero attribute words. Original84-byte header and facet array
 * exported; ASCII STL, color/attribute dialects and rendering unsupported. Signatureless detection/search is offset-zero only. File limit64MiB, member limit4096. No
 * payload or external resource is executed.
 */
#ifndef XX_STEREOLITHOGRAPHY_STL_H
#define XX_STEREOLITHOGRAPHY_STL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_stereolithography_stl {
    Abstractformat format;
} xx_stereolithography_stl;
XXFC_API void xx_stereolithography_stl_init(xx_stereolithography_stl *, xx_io_device *, int64_t);
XXFC_API xx_stereolithography_stl *xx_stereolithography_stl_create(xx_io_device *, int64_t);
XXFC_API void xx_stereolithography_stl_destroy(xx_stereolithography_stl *);
XXFC_API void xx_stereolithography_stl_free(xx_stereolithography_stl *);
XXFC_API bool xx_stereolithography_stl_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_stereolithography_stl_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_stereolithography_stl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_stereolithography_stl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_stereolithography_stl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_stereolithography_stl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_stereolithography_stl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
