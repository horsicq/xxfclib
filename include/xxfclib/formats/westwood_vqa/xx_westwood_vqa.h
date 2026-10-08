/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/westwood_vqa.c
 * Westwood FORM/WVQA v1/2 with complete VQHD/FINF frame indexes and length-framed frame/audio chunks, including IFF padding and nested encoded VQ frame chunks. Original encoded components are exported; version3/high-color/unknown extensions and rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_WESTWOOD_VQA_H
#define XX_WESTWOOD_VQA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_westwood_vqa { Abstractformat format; } xx_westwood_vqa;
XXFC_API void xx_westwood_vqa_init(xx_westwood_vqa *,xx_io_device *,int64_t);
XXFC_API xx_westwood_vqa *xx_westwood_vqa_create(xx_io_device *,int64_t);
XXFC_API void xx_westwood_vqa_destroy(xx_westwood_vqa *);
XXFC_API void xx_westwood_vqa_free(xx_westwood_vqa *);
XXFC_API bool xx_westwood_vqa_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_westwood_vqa_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_westwood_vqa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_westwood_vqa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_westwood_vqa_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
