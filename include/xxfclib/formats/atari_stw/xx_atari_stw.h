/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_ATARI_STW_H
#define XX_ATARI_STW_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_stw {
    Abstractformat format;
} xx_atari_stw;
XXFC_API void xx_atari_stw_init(xx_atari_stw *, xx_io_device *, int64_t);
XXFC_API xx_atari_stw *xx_atari_stw_create(xx_io_device *, int64_t);
XXFC_API void xx_atari_stw_destroy(xx_atari_stw *);
XXFC_API void xx_atari_stw_free(xx_atari_stw *);
XXFC_API bool xx_atari_stw_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_atari_stw_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
