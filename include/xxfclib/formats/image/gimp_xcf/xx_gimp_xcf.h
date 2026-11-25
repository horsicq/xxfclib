/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://developer.gimp.org/core/standards/xcf/
 * XCF0-3 8-bit RGB/grayscale/indexed layers/channels, complete typed property framing, hierarchy/level/tile references and stored or validated RLE tile planes. Original
 * encoded tiles and structures exported; zlib/fractal compression, newer precision versions and rendering unsupported. Layer masks and unallocated empty tile levels
 * supported; bounded editing-state property payloads retained opaque. Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_GIMP_XCF_H
#define XX_GIMP_XCF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gimp_xcf {
    Abstractformat format;
} xx_gimp_xcf;
XXFC_API void xx_gimp_xcf_init(xx_gimp_xcf *, xx_io_device *, int64_t);
XXFC_API xx_gimp_xcf *xx_gimp_xcf_create(xx_io_device *, int64_t);
XXFC_API void xx_gimp_xcf_destroy(xx_gimp_xcf *);
XXFC_API void xx_gimp_xcf_free(xx_gimp_xcf *);
XXFC_API bool xx_gimp_xcf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_gimp_xcf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gimp_xcf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gimp_xcf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gimp_xcf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gimp_xcf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gimp_xcf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
