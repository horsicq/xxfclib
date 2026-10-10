/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PALLADIX_SND_H
#define XX_PALLADIX_SND_H
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_legacy_sound_driver xx_palladix_snd;
XXFC_API void xx_palladix_snd_init(xx_palladix_snd *, xx_io_device *, int64_t);
XXFC_API xx_palladix_snd *xx_palladix_snd_create(xx_io_device *, int64_t);
XXFC_API void xx_palladix_snd_destroy(xx_palladix_snd *);
XXFC_API void xx_palladix_snd_free(xx_palladix_snd *);
static inline Abstractformat *xx_palladix_snd_to_format(xx_palladix_snd *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
