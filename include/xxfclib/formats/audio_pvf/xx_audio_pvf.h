/* SPDX-License-Identifier: MIT. Portable Voice Format PVF1/PVF2 reader. */
#ifndef XX_AUDIO_PVF_H
#define XX_AUDIO_PVF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_pvf { Abstractformat format; } xx_audio_pvf;
XXFC_API void xx_audio_pvf_init(xx_audio_pvf *, xx_io_device *, int64_t);
XXFC_API xx_audio_pvf *xx_audio_pvf_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_pvf_destroy(xx_audio_pvf *);
XXFC_API void xx_audio_pvf_free(xx_audio_pvf *);
XXFC_API bool xx_audio_pvf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_pvf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
