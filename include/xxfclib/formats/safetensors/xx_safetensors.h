/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SAFETENSORS_H
#define XX_SAFETENSORS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_safetensors {Abstractformat format;} xx_safetensors;
XXFC_API void xx_safetensors_init(xx_safetensors *,xx_io_device *,int64_t);
XXFC_API xx_safetensors *xx_safetensors_create(xx_io_device *,int64_t);
XXFC_API void xx_safetensors_destroy(xx_safetensors *);
XXFC_API void xx_safetensors_free(xx_safetensors *);
XXFC_API bool xx_safetensors_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_safetensors_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_safetensors_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_safetensors_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_safetensors_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_safetensors_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_safetensors_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
