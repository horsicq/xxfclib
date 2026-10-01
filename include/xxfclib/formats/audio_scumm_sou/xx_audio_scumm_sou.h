/* SPDX-License-Identifier: MIT. SCUMM SOU speech archive reader. */
#ifndef XX_AUDIO_SCUMM_SOU_H
#define XX_AUDIO_SCUMM_SOU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_scumm_sou { Abstractformat format; } xx_audio_scumm_sou;
XXFC_API void xx_audio_scumm_sou_init(xx_audio_scumm_sou *, xx_io_device *, int64_t);
XXFC_API xx_audio_scumm_sou *xx_audio_scumm_sou_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_scumm_sou_destroy(xx_audio_scumm_sou *);
XXFC_API void xx_audio_scumm_sou_free(xx_audio_scumm_sou *);
XXFC_API bool xx_audio_scumm_sou_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_scumm_sou_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
