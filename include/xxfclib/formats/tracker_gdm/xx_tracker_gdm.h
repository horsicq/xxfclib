/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/gdm_load.c
 * General Digital Music1.0 with157-byte header, disjoint offset-based orders/instruments/patterns/samples and no message/scroller blocks. Up to256 patterns/samples and32
 * channels. Checks full packed event fields,1-64 row terminators, sample flags/loops (GDM loopEnd minus one, in frames) and physical overlap. Exports
 * descriptor/orders/instruments/patterns and original8/16-bit sample bytes; no playback.256MiB cap.
 */
#ifndef XX_TRACKER_GDM_H
#define XX_TRACKER_GDM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_gdm {
    Abstractformat format;
} xx_tracker_gdm;
XXFC_API void xx_tracker_gdm_init(xx_tracker_gdm *, xx_io_device *, int64_t);
XXFC_API xx_tracker_gdm *xx_tracker_gdm_create(xx_io_device *, int64_t);
XXFC_API void xx_tracker_gdm_destroy(xx_tracker_gdm *);
XXFC_API void xx_tracker_gdm_free(xx_tracker_gdm *);
XXFC_API bool xx_tracker_gdm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tracker_gdm_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_gdm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_gdm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_gdm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_gdm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_gdm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
