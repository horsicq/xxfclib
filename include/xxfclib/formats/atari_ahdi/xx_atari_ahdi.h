/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ATARI_AHDI_READER_H
#define XX_ATARI_AHDI_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_atari_ahdi;
XXFC_API void xx_atari_ahdi_init(xx_atari_ahdi *, xx_io_device *, int64_t);
XXFC_API xx_atari_ahdi *xx_atari_ahdi_create(xx_io_device *, int64_t);
XXFC_API void xx_atari_ahdi_destroy(xx_atari_ahdi *);
XXFC_API void xx_atari_ahdi_free(xx_atari_ahdi *);
static inline Abstractformat *xx_atari_ahdi_to_format(xx_atari_ahdi *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
