/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/BrawlCrate/BrawlCrate/master/BrawlLib/SSBB/Types/Audio/RWAV.cs
 * Big-endian RWAV1.2 PCM8/PCM16 wave containers with1-8 channels and offset-based sample data. Checks INFO/DATA extents, channel table/records and sample ranges. Exports INFO metadata and stored PCM planes; DSP ADPCM, absolute runtime pointers and playback unsupported.
 */
#ifndef XX_NINTENDO_BRWAV_H
#define XX_NINTENDO_BRWAV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_brwav { Abstractformat format; } xx_nintendo_brwav;
XXFC_API void xx_nintendo_brwav_init(xx_nintendo_brwav *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_brwav *xx_nintendo_brwav_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_brwav_destroy(xx_nintendo_brwav *);
XXFC_API void xx_nintendo_brwav_free(xx_nintendo_brwav *);
XXFC_API bool xx_nintendo_brwav_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_brwav_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_brwav_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_brwav_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_brwav_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_brwav_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_brwav_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
