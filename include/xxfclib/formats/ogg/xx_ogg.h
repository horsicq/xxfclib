/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc3533.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_OGG_H
#define XX_OGG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ogg { Abstractformat format; } xx_ogg;
XXFC_API void xx_ogg_init(xx_ogg *,xx_io_device *,int64_t);
XXFC_API xx_ogg *xx_ogg_create(xx_io_device *,int64_t);
XXFC_API void xx_ogg_destroy(xx_ogg *);
XXFC_API void xx_ogg_free(xx_ogg *);
XXFC_API bool xx_ogg_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ogg_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ogg_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ogg_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ogg_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ogg_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ogg_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
