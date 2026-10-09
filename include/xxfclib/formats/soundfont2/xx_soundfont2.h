/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SoundFont 2 sample-bank reader: stored PCM sample streams.
 */
#ifndef XX_SOUNDFONT2_H
#define XX_SOUNDFONT2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_soundfont2 { Abstractformat format; } xx_soundfont2;

XXFC_API void xx_soundfont2_init(xx_soundfont2 *, xx_io_device *, int64_t);
XXFC_API xx_soundfont2 *xx_soundfont2_create(xx_io_device *, int64_t);
XXFC_API void xx_soundfont2_destroy(xx_soundfont2 *);
XXFC_API void xx_soundfont2_free(xx_soundfont2 *);
XXFC_API bool xx_soundfont2_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_soundfont2_handle_base_info(Abstractformat *, xx_pd_struct *);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_soundfont2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_soundfont2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_soundfont2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_soundfont2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_soundfont2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
