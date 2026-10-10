/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HP_LIF_READER_H
#define XX_HP_LIF_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_hp_lif;
XXFC_API void xx_hp_lif_init(xx_hp_lif *, xx_io_device *, int64_t);
XXFC_API xx_hp_lif *xx_hp_lif_create(xx_io_device *, int64_t);
XXFC_API void xx_hp_lif_destroy(xx_hp_lif *);
XXFC_API void xx_hp_lif_free(xx_hp_lif *);
static inline Abstractformat *xx_hp_lif_to_format(xx_hp_lif *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
