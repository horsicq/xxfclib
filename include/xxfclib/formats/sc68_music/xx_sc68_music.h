/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_SC68_MUSIC_H
#define XX_SC68_MUSIC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sc68_music { Abstractformat format; } xx_sc68_music;
XXFC_API void xx_sc68_music_init(xx_sc68_music *,xx_io_device *,int64_t);
XXFC_API xx_sc68_music *xx_sc68_music_create(xx_io_device *,int64_t);
XXFC_API void xx_sc68_music_destroy(xx_sc68_music *);
XXFC_API void xx_sc68_music_free(xx_sc68_music *);
XXFC_API bool xx_sc68_music_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sc68_music_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sc68_music_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sc68_music_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sc68_music_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sc68_music_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sc68_music_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
