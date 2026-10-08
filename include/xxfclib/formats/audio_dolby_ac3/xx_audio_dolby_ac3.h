/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/ac3_parser.c
 * Classic AC3 bsid8 raw streams with stable sample rate/channel mode and complete BSI optional fields. Exact declared frame sizes and both independent CRC16 regions are checked. Original complete encoded frames are exported; EAC3, other bitstream IDs, byte-swapped streams and audio decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_AUDIO_DOLBY_AC3_H
#define XX_AUDIO_DOLBY_AC3_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_dolby_ac3 { Abstractformat format; } xx_audio_dolby_ac3;
XXFC_API void xx_audio_dolby_ac3_init(xx_audio_dolby_ac3 *,xx_io_device *,int64_t);
XXFC_API xx_audio_dolby_ac3 *xx_audio_dolby_ac3_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_dolby_ac3_destroy(xx_audio_dolby_ac3 *);
XXFC_API void xx_audio_dolby_ac3_free(xx_audio_dolby_ac3 *);
XXFC_API bool xx_audio_dolby_ac3_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_dolby_ac3_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_audio_dolby_ac3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_audio_dolby_ac3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_audio_dolby_ac3_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
