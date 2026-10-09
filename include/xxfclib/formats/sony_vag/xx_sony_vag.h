/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/vgmstream/vgmstream/master/src/meta/vag.c
 * Standard big-endian mono VAGp version0x20 with 48-byte header. Exports encoded PS-ADPCM frames after validating frame predictor/shift/flag fields. Stereo/interleaved/game-specific headers and PCM decoding unsupported.
 */
#ifndef XX_SONY_VAG_H
#define XX_SONY_VAG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_vag { Abstractformat format; } xx_sony_vag;
XXFC_API void xx_sony_vag_init(xx_sony_vag *,xx_io_device *,int64_t);
XXFC_API xx_sony_vag *xx_sony_vag_create(xx_io_device *,int64_t);
XXFC_API void xx_sony_vag_destroy(xx_sony_vag *);
XXFC_API void xx_sony_vag_free(xx_sony_vag *);
XXFC_API bool xx_sony_vag_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sony_vag_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_vag_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_vag_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_vag_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_vag_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_vag_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
