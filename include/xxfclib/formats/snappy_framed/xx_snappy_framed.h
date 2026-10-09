/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_SNAPPY_FRAMED_H
#define XX_SNAPPY_FRAMED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_snappy_framed { Abstractformat format; } xx_snappy_framed;
XXFC_API void xx_snappy_framed_init(xx_snappy_framed *,xx_io_device *,int64_t);
XXFC_API xx_snappy_framed *xx_snappy_framed_create(xx_io_device *,int64_t);
XXFC_API void xx_snappy_framed_destroy(xx_snappy_framed *);
XXFC_API void xx_snappy_framed_free(xx_snappy_framed *);
XXFC_API bool xx_snappy_framed_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_snappy_framed_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_snappy_framed_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_snappy_framed_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_snappy_framed_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_snappy_framed_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_snappy_framed_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
