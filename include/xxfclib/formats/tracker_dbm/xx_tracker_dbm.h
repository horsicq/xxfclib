/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/dbm_load.c
 * DigiBooster Pro2 DBM0 with NAME/INFO/SONG/INST/PATT/SMPL complete chunk tables, one song, up to256 patterns/instruments/samples and64 channels. Checks row packet fields, instrument/sample references, sample formats8/16/32-bit, loop spans and complete chunk consumption, with at most four zero row-padding bytes per pattern. Exports encoded chunks without playback. Envelope/echo/other extension chunks unsupported.256MiB cap.
 */
#ifndef XX_TRACKER_DBM_H
#define XX_TRACKER_DBM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_dbm { Abstractformat format; } xx_tracker_dbm;
XXFC_API void xx_tracker_dbm_init(xx_tracker_dbm *,xx_io_device *,int64_t);
XXFC_API xx_tracker_dbm *xx_tracker_dbm_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_dbm_destroy(xx_tracker_dbm *);
XXFC_API void xx_tracker_dbm_free(xx_tracker_dbm *);
XXFC_API bool xx_tracker_dbm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_dbm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_dbm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_dbm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_dbm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
