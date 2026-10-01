/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/ult_load.c, https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_ult.cpp
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_TRACKER_ULT_H
#define XX_TRACKER_ULT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_ult { Abstractformat format; } xx_tracker_ult;
XXFC_API void xx_tracker_ult_init(xx_tracker_ult *,xx_io_device *,int64_t);
XXFC_API xx_tracker_ult *xx_tracker_ult_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_ult_destroy(xx_tracker_ult *);
XXFC_API void xx_tracker_ult_free(xx_tracker_ult *);
XXFC_API bool xx_tracker_ult_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_ult_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
