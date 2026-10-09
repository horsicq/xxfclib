/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/ipmovie.c
 * Interplay MVE standard26-byte preamble, typed chunk/opcode framing, bounded video/audio/palette initialization and complete shutdown/end chunks. Original encoded chunks are exported; video/audio codec commands and documented opaque opcodes18-21 remain encoded, and unknown opcode/version variants are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_INTERPLAY_MVE_H
#define XX_INTERPLAY_MVE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_interplay_mve { Abstractformat format; } xx_interplay_mve;
XXFC_API void xx_interplay_mve_init(xx_interplay_mve *,xx_io_device *,int64_t);
XXFC_API xx_interplay_mve *xx_interplay_mve_create(xx_io_device *,int64_t);
XXFC_API void xx_interplay_mve_destroy(xx_interplay_mve *);
XXFC_API void xx_interplay_mve_free(xx_interplay_mve *);
XXFC_API bool xx_interplay_mve_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_interplay_mve_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_interplay_mve_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_interplay_mve_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_interplay_mve_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_interplay_mve_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_interplay_mve_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
