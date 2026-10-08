/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/far_load.c
 * Farandole Composer1.0 complete header/order tables, up to256 stored16-channel patterns and64 stored8/16-bit samples. Validates event references, row counts, loop extents and sample bitmap; exports descriptor/comment, patterns, instrument records and samples. Header extensions preserved; no playback.256MiB cap.
 */
#ifndef XX_TRACKER_FAR_H
#define XX_TRACKER_FAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_far { Abstractformat format; } xx_tracker_far;
XXFC_API void xx_tracker_far_init(xx_tracker_far *,xx_io_device *,int64_t);
XXFC_API xx_tracker_far *xx_tracker_far_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_far_destroy(xx_tracker_far *);
XXFC_API void xx_tracker_far_free(xx_tracker_far *);
XXFC_API bool xx_tracker_far_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_far_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_far_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_far_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_far_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
