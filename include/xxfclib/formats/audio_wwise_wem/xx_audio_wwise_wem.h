/* SPDX-License-Identifier: MIT. Wwise RIFF stored-chunk reader. */
#ifndef XX_AUDIO_WWISE_WEM_H
#define XX_AUDIO_WWISE_WEM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_wwise_wem { Abstractformat format; } xx_audio_wwise_wem;
XXFC_API void xx_audio_wwise_wem_init(xx_audio_wwise_wem *, xx_io_device *, int64_t);
XXFC_API xx_audio_wwise_wem *xx_audio_wwise_wem_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_wwise_wem_destroy(xx_audio_wwise_wem *);
XXFC_API void xx_audio_wwise_wem_free(xx_audio_wwise_wem *);
XXFC_API bool xx_audio_wwise_wem_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_wwise_wem_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
