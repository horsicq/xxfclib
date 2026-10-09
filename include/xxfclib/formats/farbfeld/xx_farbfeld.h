/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://git.suckless.org/farbfeld/file/FORMAT.html
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_FARBFELD_H
#define XX_FARBFELD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_farbfeld { Abstractformat format; } xx_farbfeld;
XXFC_API void xx_farbfeld_init(xx_farbfeld *,xx_io_device *,int64_t);
XXFC_API xx_farbfeld *xx_farbfeld_create(xx_io_device *,int64_t);
XXFC_API void xx_farbfeld_destroy(xx_farbfeld *);
XXFC_API void xx_farbfeld_free(xx_farbfeld *);
XXFC_API bool xx_farbfeld_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_farbfeld_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_farbfeld_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_farbfeld_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_farbfeld_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_farbfeld_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_farbfeld_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
