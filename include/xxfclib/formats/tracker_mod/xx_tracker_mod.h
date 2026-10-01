/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mod_load.c
 * 31-sample four-channel ProTracker-compatible MOD with M.K./M!K!/4CHN/FLT4 signature,1-128 orders/patterns and original signed8-bit samples. Checks order/sample/note references, loops and all physical extents; exports descriptor, each1024-byte pattern and sample.15-sample/signatureless modules, alternate channels and packed/external samples rejected; no playback.256MiB physical cap.
 */
#ifndef XX_TRACKER_MOD_H
#define XX_TRACKER_MOD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_mod { Abstractformat format; } xx_tracker_mod;
XXFC_API void xx_tracker_mod_init(xx_tracker_mod *,xx_io_device *,int64_t);
XXFC_API xx_tracker_mod *xx_tracker_mod_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_mod_destroy(xx_tracker_mod *);
XXFC_API void xx_tracker_mod_free(xx_tracker_mod *);
XXFC_API bool xx_tracker_mod_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_mod_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
