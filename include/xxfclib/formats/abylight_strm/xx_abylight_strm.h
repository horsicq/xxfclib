/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Abylight Nintendo 3DS STRM AAC stream stored-component reader.
 */
#ifndef XX_ABYLIGHT_STRM_H
#define XX_ABYLIGHT_STRM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_abylight_strm { Abstractformat format; } xx_abylight_strm;
XXFC_API void xx_abylight_strm_init(xx_abylight_strm *,xx_io_device *,int64_t);
XXFC_API xx_abylight_strm *xx_abylight_strm_create(xx_io_device *,int64_t);
XXFC_API void xx_abylight_strm_destroy(xx_abylight_strm *);
XXFC_API void xx_abylight_strm_free(xx_abylight_strm *);
XXFC_API bool xx_abylight_strm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_abylight_strm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_abylight_strm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_abylight_strm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_abylight_strm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_abylight_strm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_abylight_strm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
