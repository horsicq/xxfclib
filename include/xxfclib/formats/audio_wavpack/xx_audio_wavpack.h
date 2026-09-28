/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AUDIO_WAVPACK_H
#define XX_AUDIO_WAVPACK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_wavpack { Abstractformat format; } xx_audio_wavpack;
XXFC_API void xx_audio_wavpack_init(xx_audio_wavpack *,xx_io_device *,int64_t);
XXFC_API xx_audio_wavpack *xx_audio_wavpack_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_wavpack_destroy(xx_audio_wavpack *);
XXFC_API void xx_audio_wavpack_free(xx_audio_wavpack *);
XXFC_API bool xx_audio_wavpack_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_wavpack_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
