/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PALLADIX_MUS_H
#define XX_PALLADIX_MUS_H
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_legacy_sound_driver xx_palladix_mus;
XXFC_API void xx_palladix_mus_init(xx_palladix_mus *, xx_io_device *, int64_t);
XXFC_API xx_palladix_mus *xx_palladix_mus_create(xx_io_device *, int64_t);
XXFC_API void xx_palladix_mus_destroy(xx_palladix_mus *);
XXFC_API void xx_palladix_mus_free(xx_palladix_mus *);
static inline Abstractformat *xx_palladix_mus_to_format(xx_palladix_mus *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
