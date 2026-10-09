/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_psm.cpp
 * New Epic MASI PSM FILE/MAINSONG containers with regular4-byte pattern IDs, up to256 patterns/samples/32 channels and one song. Validates chunk tiling, OPLH playlist/settings opcode framing, packed row extents (including well-framed inactive rows, at most256 total), all used sample identities/loops/rates and order references. Exports original chunks; sample bytes remain delta-coded, no decoding/playback. PSM16/Sinaria, unknown opcodes/chunks and extension modes rejected.256MiB cap.
 */
#ifndef XX_TRACKER_PSM_H
#define XX_TRACKER_PSM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_psm { Abstractformat format; } xx_tracker_psm;
XXFC_API void xx_tracker_psm_init(xx_tracker_psm *,xx_io_device *,int64_t);
XXFC_API xx_tracker_psm *xx_tracker_psm_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_psm_destroy(xx_tracker_psm *);
XXFC_API void xx_tracker_psm_free(xx_tracker_psm *);
XXFC_API bool xx_tracker_psm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_psm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_psm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_psm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_psm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_psm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_psm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
