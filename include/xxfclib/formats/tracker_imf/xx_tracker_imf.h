/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/imf_load.c
 * Imago Orpheus IMF1.0 with832-byte descriptor, up to256 patterns/instruments and32 channels; instrument II10/sample IS10 records, PCM8/16. Validates packed row events, multisample maps, bounded envelopes, sample loops/rates and complete sequential extents. Exports descriptor/patterns/instrument/sample headers and original PCM. Alternate IW10, compression and unknown extensions rejected; no playback.256MiB cap/4096 output components.
 */
#ifndef XX_TRACKER_IMF_H
#define XX_TRACKER_IMF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_imf { Abstractformat format; } xx_tracker_imf;
XXFC_API void xx_tracker_imf_init(xx_tracker_imf *,xx_io_device *,int64_t);
XXFC_API xx_tracker_imf *xx_tracker_imf_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_imf_destroy(xx_tracker_imf *);
XXFC_API void xx_tracker_imf_free(xx_tracker_imf *);
XXFC_API bool xx_tracker_imf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_imf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_imf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_imf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_imf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
