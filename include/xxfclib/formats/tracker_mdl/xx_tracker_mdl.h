/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mdl_load.c
 * DigiTrakker MDL1.0/1.1 typed chunks with IN/PA/TR/II/IS/SA and optional VE/PE/FE/ME, up to256 patterns/instruments/samples and4096 tracks. Checks packed track commands, instrument/sample references, envelope records, exact stored or length-framed packed sample extents. Exports complete original chunks; compressed sample bytes and effects remain encoded, no decoding/playback.256MiB cap.
 */
#ifndef XX_TRACKER_MDL_H
#define XX_TRACKER_MDL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_mdl { Abstractformat format; } xx_tracker_mdl;
XXFC_API void xx_tracker_mdl_init(xx_tracker_mdl *,xx_io_device *,int64_t);
XXFC_API xx_tracker_mdl *xx_tracker_mdl_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_mdl_destroy(xx_tracker_mdl *);
XXFC_API void xx_tracker_mdl_free(xx_tracker_mdl *);
XXFC_API bool xx_tracker_mdl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_mdl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_mdl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_mdl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_mdl_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
