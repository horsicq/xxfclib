/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/vgmstream/vgmstream/master/src/meta/bfwav.c
 * CWAV versions0x102/0x10102, either BOM, one or two PCM8/PCM16 channels. Exports INFO and exact encoded channel bytes; validates channel/data references and disjoint extents. ADPCM/IMA and playback unsupported.
 */
#ifndef XX_NINTENDO_BCWAV_H
#define XX_NINTENDO_BCWAV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bcwav { Abstractformat format; } xx_nintendo_bcwav;
XXFC_API void xx_nintendo_bcwav_init(xx_nintendo_bcwav *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bcwav *xx_nintendo_bcwav_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bcwav_destroy(xx_nintendo_bcwav *);
XXFC_API void xx_nintendo_bcwav_free(xx_nintendo_bcwav *);
XXFC_API bool xx_nintendo_bcwav_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bcwav_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bcwav_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_bcwav_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bcwav_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bcwav_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_bcwav_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
