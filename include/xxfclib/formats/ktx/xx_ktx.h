/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html
 * KTX1 endian-aware key/value data and encoded mip levels; no pixel decoding or byte-swapping. Array/volume mip data stays grouped.
 */
#ifndef XX_KTX_H
#define XX_KTX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ktx { Abstractformat format; } xx_ktx;
XXFC_API void xx_ktx_init(xx_ktx *,xx_io_device *,int64_t);
XXFC_API xx_ktx *xx_ktx_create(xx_io_device *,int64_t);
XXFC_API void xx_ktx_destroy(xx_ktx *);
XXFC_API void xx_ktx_free(xx_ktx *);
XXFC_API bool xx_ktx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ktx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ktx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ktx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ktx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ktx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ktx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
