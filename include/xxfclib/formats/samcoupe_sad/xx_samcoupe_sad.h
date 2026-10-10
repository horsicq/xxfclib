/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only SAD side-sequential sector image; exact extraction scope is documented in the source.
 */
#ifndef XX_SAMCOUPE_SAD_H
#define XX_SAMCOUPE_SAD_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_samcoupe_sad;
XXFC_API void xx_samcoupe_sad_init(xx_samcoupe_sad *, xx_io_device *, int64_t);
XXFC_API xx_samcoupe_sad *xx_samcoupe_sad_create(xx_io_device *, int64_t);
XXFC_API void xx_samcoupe_sad_destroy(xx_samcoupe_sad *);
XXFC_API void xx_samcoupe_sad_free(xx_samcoupe_sad *);
static inline Abstractformat *xx_samcoupe_sad_to_format(xx_samcoupe_sad *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
