/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AUDIO_DSF_H
#define XX_AUDIO_DSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_dsf { Abstractformat format; } xx_audio_dsf;
XXFC_API void xx_audio_dsf_init(xx_audio_dsf *,xx_io_device *,int64_t);
XXFC_API xx_audio_dsf *xx_audio_dsf_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_dsf_destroy(xx_audio_dsf *);
XXFC_API void xx_audio_dsf_free(xx_audio_dsf *);
XXFC_API bool xx_audio_dsf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_dsf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
