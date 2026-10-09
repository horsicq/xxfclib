/* SPDX-License-Identifier: MIT */
#ifndef XX_IMPLAY_MUSIC_H
#define XX_IMPLAY_MUSIC_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_implay_music {Abstractformat format;} xx_implay_music;
XXFC_API void xx_implay_music_init(xx_implay_music *,xx_io_device *,int64_t);
XXFC_API xx_implay_music *xx_implay_music_create(xx_io_device *,int64_t);
XXFC_API void xx_implay_music_destroy(xx_implay_music *);
XXFC_API void xx_implay_music_free(xx_implay_music *);
XXFC_API bool xx_implay_music_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_implay_music_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_implay_music_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_implay_music_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_implay_music_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_implay_music_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_implay_music_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
