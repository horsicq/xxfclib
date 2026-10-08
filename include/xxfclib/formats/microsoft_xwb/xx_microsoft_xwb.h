/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/microsoft/DirectXTK/main/Audio/WaveBankReader.cpp
 * XACT wave-bank header version44, little-endian noncompact buffered banks with up to1024 PCM8/PCM16 entries and optional fixed-size names. Validates five disjoint segments, mini-format, duration/loop and wave extents. Exports encoded PCM entries; streaming/compact/ADPCM/XMA/WMA/seek tables and playback unsupported.
 */
#ifndef XX_MICROSOFT_XWB_H
#define XX_MICROSOFT_XWB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microsoft_xwb { Abstractformat format; } xx_microsoft_xwb;
XXFC_API void xx_microsoft_xwb_init(xx_microsoft_xwb *,xx_io_device *,int64_t);
XXFC_API xx_microsoft_xwb *xx_microsoft_xwb_create(xx_io_device *,int64_t);
XXFC_API void xx_microsoft_xwb_destroy(xx_microsoft_xwb *);
XXFC_API void xx_microsoft_xwb_free(xx_microsoft_xwb *);
XXFC_API bool xx_microsoft_xwb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microsoft_xwb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microsoft_xwb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microsoft_xwb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microsoft_xwb_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
