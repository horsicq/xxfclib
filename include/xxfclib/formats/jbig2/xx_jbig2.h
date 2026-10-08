/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.itu.int/rec/T-REC-T.88-201808-I/en, https://raw.githubusercontent.com/ArtifexSoftware/jbig2dec/master/jbig2_segment.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_JBIG2_H
#define XX_JBIG2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jbig2 { Abstractformat format; } xx_jbig2;
XXFC_API void xx_jbig2_init(xx_jbig2 *,xx_io_device *,int64_t);
XXFC_API xx_jbig2 *xx_jbig2_create(xx_io_device *,int64_t);
XXFC_API void xx_jbig2_destroy(xx_jbig2 *);
XXFC_API void xx_jbig2_free(xx_jbig2 *);
XXFC_API bool xx_jbig2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jbig2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jbig2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jbig2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jbig2_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
