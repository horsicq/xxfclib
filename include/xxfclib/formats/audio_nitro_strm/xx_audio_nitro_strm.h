/* SPDX-License-Identifier: MIT. Nintendo DS STRM encoded-block reader. */
#ifndef XX_AUDIO_NITRO_STRM_H
#define XX_AUDIO_NITRO_STRM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_nitro_strm { Abstractformat format; } xx_audio_nitro_strm;
XXFC_API void xx_audio_nitro_strm_init(xx_audio_nitro_strm *, xx_io_device *, int64_t);
XXFC_API xx_audio_nitro_strm *xx_audio_nitro_strm_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_nitro_strm_destroy(xx_audio_nitro_strm *);
XXFC_API void xx_audio_nitro_strm_free(xx_audio_nitro_strm *);
XXFC_API bool xx_audio_nitro_strm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_nitro_strm_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
