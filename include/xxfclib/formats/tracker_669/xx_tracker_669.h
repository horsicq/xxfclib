/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/669_load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_TRACKER_669_H
#define XX_TRACKER_669_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_669 { Abstractformat format; } xx_tracker_669;
XXFC_API void xx_tracker_669_init(xx_tracker_669 *,xx_io_device *,int64_t);
XXFC_API xx_tracker_669 *xx_tracker_669_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_669_destroy(xx_tracker_669 *);
XXFC_API void xx_tracker_669_free(xx_tracker_669 *);
XXFC_API bool xx_tracker_669_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_669_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
