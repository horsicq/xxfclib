/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_HES_SOUND_H
#define XX_HES_SOUND_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hes_sound {
    Abstractformat format;
} xx_hes_sound;
XXFC_API void xx_hes_sound_init(xx_hes_sound *, xx_io_device *, int64_t);
XXFC_API xx_hes_sound *xx_hes_sound_create(xx_io_device *, int64_t);
XXFC_API void xx_hes_sound_destroy(xx_hes_sound *);
XXFC_API void xx_hes_sound_free(xx_hes_sound *);
XXFC_API bool xx_hes_sound_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hes_sound_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hes_sound_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_hes_sound_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hes_sound_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hes_sound_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_hes_sound_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
