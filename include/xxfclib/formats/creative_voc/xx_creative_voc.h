/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/voc.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_CREATIVE_VOC_H
#define XX_CREATIVE_VOC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_creative_voc { Abstractformat format; } xx_creative_voc;
XXFC_API void xx_creative_voc_init(xx_creative_voc *,xx_io_device *,int64_t);
XXFC_API xx_creative_voc *xx_creative_voc_create(xx_io_device *,int64_t);
XXFC_API void xx_creative_voc_destroy(xx_creative_voc *);
XXFC_API void xx_creative_voc_free(xx_creative_voc *);
XXFC_API bool xx_creative_voc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_creative_voc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
