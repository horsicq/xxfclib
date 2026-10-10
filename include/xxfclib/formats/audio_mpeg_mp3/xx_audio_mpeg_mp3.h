/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/mpegaudiodecheader.c
 * MPEG1 LayerIII unprotected frames, stable sample rate/channel count, complete side-information grammar and bounded bit-reservoir/part lengths. Optional unflagged
 * ID3v2.3/2.4 frames and padding are fully length-framed. Original metadata and encoded audio frames are exported; MPEG2/2.5, free bitrate, protected frames,
 * ID3v1/extended/unsynchronised tags and audio decoding are unsupported. File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_AUDIO_MPEG_MP3_H
#define XX_AUDIO_MPEG_MP3_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_mpeg_mp3 {
    Abstractformat format;
} xx_audio_mpeg_mp3;
XXFC_API void xx_audio_mpeg_mp3_init(xx_audio_mpeg_mp3 *, xx_io_device *, int64_t);
XXFC_API xx_audio_mpeg_mp3 *xx_audio_mpeg_mp3_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_mpeg_mp3_destroy(xx_audio_mpeg_mp3 *);
XXFC_API void xx_audio_mpeg_mp3_free(xx_audio_mpeg_mp3 *);
XXFC_API bool xx_audio_mpeg_mp3_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_mpeg_mp3_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_audio_mpeg_mp3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_audio_mpeg_mp3_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_audio_mpeg_mp3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_audio_mpeg_mp3_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_audio_mpeg_mp3_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
