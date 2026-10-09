/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://openexr.com/en/latest/OpenEXRFileLayout.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_OPENEXR_H
#define XX_OPENEXR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openexr { Abstractformat format; } xx_openexr;
XXFC_API void xx_openexr_init(xx_openexr *,xx_io_device *,int64_t);
XXFC_API xx_openexr *xx_openexr_create(xx_io_device *,int64_t);
XXFC_API void xx_openexr_destroy(xx_openexr *);
XXFC_API void xx_openexr_free(xx_openexr *);
XXFC_API bool xx_openexr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openexr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_openexr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_openexr_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_openexr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_openexr_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_openexr_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
