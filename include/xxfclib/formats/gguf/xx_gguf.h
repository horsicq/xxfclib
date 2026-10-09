/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_GGUF_H
#define XX_GGUF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gguf {Abstractformat format;} xx_gguf;
XXFC_API void xx_gguf_init(xx_gguf *,xx_io_device *,int64_t);
XXFC_API xx_gguf *xx_gguf_create(xx_io_device *,int64_t);
XXFC_API void xx_gguf_destroy(xx_gguf *);
XXFC_API void xx_gguf_free(xx_gguf *);
XXFC_API bool xx_gguf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gguf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gguf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gguf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gguf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gguf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gguf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
