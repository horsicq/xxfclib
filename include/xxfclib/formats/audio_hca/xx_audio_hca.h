/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AUDIO_HCA_H
#define XX_AUDIO_HCA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_hca { Abstractformat format; } xx_audio_hca;
XXFC_API void xx_audio_hca_init(xx_audio_hca *,xx_io_device *,int64_t);
XXFC_API xx_audio_hca *xx_audio_hca_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_hca_destroy(xx_audio_hca *);
XXFC_API void xx_audio_hca_free(xx_audio_hca *);
XXFC_API bool xx_audio_hca_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_hca_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
