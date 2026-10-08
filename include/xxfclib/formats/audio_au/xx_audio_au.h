/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/au.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_AUDIO_AU_H
#define XX_AUDIO_AU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_au { Abstractformat format; } xx_audio_au;
XXFC_API void xx_audio_au_init(xx_audio_au *,xx_io_device *,int64_t);
XXFC_API xx_audio_au *xx_audio_au_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_au_destroy(xx_audio_au *);
XXFC_API void xx_audio_au_free(xx_audio_au *);
XXFC_API bool xx_audio_au_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_au_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_audio_au_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_audio_au_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_audio_au_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
