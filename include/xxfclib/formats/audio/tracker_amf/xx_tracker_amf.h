/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/amf_load.c
 * DSMI AMF1.4 with bounded logical/physical track mappings,1-32 channels/256 patterns/255 instruments/4096 tracks, complete three-byte track event records, including
 * zero-event physical tracks and original sequential8-bit samples. Checks row/instrument references, sample loops and all physical lengths. Exports
 * descriptor/orders/instruments/map/tracks and PCM. Earlier AMF revisions and Asylum AMF are rejected; no playback.256MiB cap.
 */
#ifndef XX_TRACKER_AMF_H
#define XX_TRACKER_AMF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_amf {
    Abstractformat format;
} xx_tracker_amf;
XXFC_API void xx_tracker_amf_init(xx_tracker_amf *, xx_io_device *, int64_t);
XXFC_API xx_tracker_amf *xx_tracker_amf_create(xx_io_device *, int64_t);
XXFC_API void xx_tracker_amf_destroy(xx_tracker_amf *);
XXFC_API void xx_tracker_amf_free(xx_tracker_amf *);
XXFC_API bool xx_tracker_amf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tracker_amf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_amf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_amf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_amf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_amf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_amf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
