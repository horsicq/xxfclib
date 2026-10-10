/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/BrawlCrate/BrawlCrate/master/BrawlLib/SSBB/Types/Audio/RSTM.cs
 * Big-endian RSTM1.0 PCM8/PCM16 streams with one block and1-2 channels, one mono/stereo track, standard HEAD/DATA references and no ADPC block. Checks disjoint HEAD
 * records and exports encoded HEAD metadata and each stored PCM channel. Multi-block/multitrack streams, DSP ADPCM, alternate beta headers and audio playback
 * unsupported.
 */
#ifndef XX_NINTENDO_BRSTM_H
#define XX_NINTENDO_BRSTM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_brstm {
    Abstractformat format;
} xx_nintendo_brstm;
XXFC_API void xx_nintendo_brstm_init(xx_nintendo_brstm *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_brstm *xx_nintendo_brstm_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_brstm_destroy(xx_nintendo_brstm *);
XXFC_API void xx_nintendo_brstm_free(xx_nintendo_brstm *);
XXFC_API bool xx_nintendo_brstm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_brstm_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_brstm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_brstm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_brstm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_brstm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_brstm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
