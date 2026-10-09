/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. WOFF 2.0 single fonts with null-transformed tables, Brotli metadata/private data. Transformed glyf/hmtx and font collections rejected; no rendering.
 */
#ifndef XX_WOFF2_H
#define XX_WOFF2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_woff2 { Abstractformat format; } xx_woff2;
XXFC_API void xx_woff2_init(xx_woff2 *,xx_io_device *,int64_t);
XXFC_API xx_woff2 *xx_woff2_create(xx_io_device *,int64_t);
XXFC_API void xx_woff2_destroy(xx_woff2 *);
XXFC_API void xx_woff2_free(xx_woff2 *);
XXFC_API bool xx_woff2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_woff2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_woff2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_woff2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_woff2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_woff2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_woff2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
