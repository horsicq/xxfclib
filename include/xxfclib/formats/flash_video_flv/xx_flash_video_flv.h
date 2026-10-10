/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/flv.h
 * Classic FLV1 with audio/video flags, complete11-byte tag framing and every PreviousTagSize backlink. Audio/video packet headers checked and AMF0 script values
 * recursively parsed. Up to4095 tags,24 AMF levels/65536 values,64MiB file. Exports original encoded tag payloads; codec decoding, playback, encrypted/filter tags, AMF3
 * and enhanced FLV unsupported.
 */
#ifndef XX_FLASH_VIDEO_FLV_H
#define XX_FLASH_VIDEO_FLV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_flash_video_flv {
    Abstractformat format;
} xx_flash_video_flv;
XXFC_API void xx_flash_video_flv_init(xx_flash_video_flv *, xx_io_device *, int64_t);
XXFC_API xx_flash_video_flv *xx_flash_video_flv_create(xx_io_device *, int64_t);
XXFC_API void xx_flash_video_flv_destroy(xx_flash_video_flv *);
XXFC_API void xx_flash_video_flv_free(xx_flash_video_flv *);
XXFC_API bool xx_flash_video_flv_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_flash_video_flv_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_flash_video_flv_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_flash_video_flv_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_flash_video_flv_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_flash_video_flv_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_flash_video_flv_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
