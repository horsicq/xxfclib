/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AUDIO_ADX_H
#define XX_AUDIO_ADX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_adx { Abstractformat format; } xx_audio_adx;
XXFC_API void xx_audio_adx_init(xx_audio_adx *,xx_io_device *,int64_t);
XXFC_API xx_audio_adx *xx_audio_adx_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_adx_destroy(xx_audio_adx *);
XXFC_API void xx_audio_adx_free(xx_audio_adx *);
XXFC_API bool xx_audio_adx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_adx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_audio_adx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_audio_adx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_audio_adx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
