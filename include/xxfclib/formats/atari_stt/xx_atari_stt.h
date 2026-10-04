/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_ATARI_STT_H
#define XX_ATARI_STT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_stt {Abstractformat format;} xx_atari_stt;
XXFC_API void xx_atari_stt_init(xx_atari_stt *,xx_io_device *,int64_t);
XXFC_API xx_atari_stt *xx_atari_stt_create(xx_io_device *,int64_t);
XXFC_API void xx_atari_stt_destroy(xx_atari_stt *);
XXFC_API void xx_atari_stt_free(xx_atari_stt *);
XXFC_API bool xx_atari_stt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_atari_stt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
