/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_SC68_MUSIC_H
#define XX_SC68_MUSIC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sc68_music { Abstractformat format; } xx_sc68_music;
XXFC_API void xx_sc68_music_init(xx_sc68_music *,xx_io_device *,int64_t);
XXFC_API xx_sc68_music *xx_sc68_music_create(xx_io_device *,int64_t);
XXFC_API void xx_sc68_music_destroy(xx_sc68_music *);
XXFC_API void xx_sc68_music_free(xx_sc68_music *);
XXFC_API bool xx_sc68_music_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sc68_music_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
