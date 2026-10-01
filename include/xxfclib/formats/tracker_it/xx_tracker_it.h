/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/it_load.c, https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/it.h
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_TRACKER_IT_H
#define XX_TRACKER_IT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_it { Abstractformat format; } xx_tracker_it;
XXFC_API void xx_tracker_it_init(xx_tracker_it *,xx_io_device *,int64_t);
XXFC_API xx_tracker_it *xx_tracker_it_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_it_destroy(xx_tracker_it *);
XXFC_API void xx_tracker_it_free(xx_tracker_it *);
XXFC_API bool xx_tracker_it_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_it_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
