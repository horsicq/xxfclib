/* SPDX-License-Identifier: MIT */
#ifndef XX_AMOS_MUSIC_BANK_H
#define XX_AMOS_MUSIC_BANK_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_amos_music_bank {Abstractformat format;} xx_amos_music_bank;
XXFC_API void xx_amos_music_bank_init(xx_amos_music_bank *,xx_io_device *,int64_t);
XXFC_API xx_amos_music_bank *xx_amos_music_bank_create(xx_io_device *,int64_t);
XXFC_API void xx_amos_music_bank_destroy(xx_amos_music_bank *);
XXFC_API void xx_amos_music_bank_free(xx_amos_music_bank *);
XXFC_API bool xx_amos_music_bank_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amos_music_bank_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amos_music_bank_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amos_music_bank_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amos_music_bank_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
