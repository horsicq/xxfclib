/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PARSEC_DMA_H
#define XX_PARSEC_DMA_H
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_legacy_sound_driver xx_parsec_dma;
XXFC_API void xx_parsec_dma_init(xx_parsec_dma *, xx_io_device *, int64_t);
XXFC_API xx_parsec_dma *xx_parsec_dma_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_dma_destroy(xx_parsec_dma *);
XXFC_API void xx_parsec_dma_free(xx_parsec_dma *);
static inline Abstractformat *xx_parsec_dma_to_format(xx_parsec_dma *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
