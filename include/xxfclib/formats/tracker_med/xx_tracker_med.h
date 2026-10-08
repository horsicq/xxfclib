/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mmd1_load.c
 * OctaMED MMD0/MMD1 one-song containers, up to255 patterns/63 stored mono8-bit samples/32 channels/3200 rows. Checks pointer tables, disjoint metadata/pattern/sample extents, note/sample references, order list and repeat ranges. Optional80-byte expansion supports bounded instrument extension/name records, annotation and song name only. Exports original song/tables/patterns/sample/metadata records. MMD2/MMD3, synthetic/multioctave/packed/stereo/external samples, linked songs and other expansion pointers rejected; no playback.256MiB cap.
 */
#ifndef XX_TRACKER_MED_H
#define XX_TRACKER_MED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_med { Abstractformat format; } xx_tracker_med;
XXFC_API void xx_tracker_med_init(xx_tracker_med *,xx_io_device *,int64_t);
XXFC_API xx_tracker_med *xx_tracker_med_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_med_destroy(xx_tracker_med *);
XXFC_API void xx_tracker_med_free(xx_tracker_med *);
XXFC_API bool xx_tracker_med_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_med_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_med_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_med_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_med_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
