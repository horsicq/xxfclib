/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AUDIO_DFF_H
#define XX_AUDIO_DFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_dff { Abstractformat format; } xx_audio_dff;
XXFC_API void xx_audio_dff_init(xx_audio_dff *,xx_io_device *,int64_t);
XXFC_API xx_audio_dff *xx_audio_dff_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_dff_destroy(xx_audio_dff *);
XXFC_API void xx_audio_dff_free(xx_audio_dff *);
XXFC_API bool xx_audio_dff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_dff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
