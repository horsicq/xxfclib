/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/parsec_dma/xx_parsec_dma.h"
#include "xxfclib/memory/xx_memory.h"
void xx_parsec_dma_init(xx_parsec_dma *r,xx_io_device *d,int64_t base) {
    xx_legacy_sound_driver_init(r,d,base,XX_FILE_TYPE_PARSEC_DMA,"dma");
}
xx_parsec_dma *xx_parsec_dma_create(xx_io_device *d,int64_t base) {
    xx_parsec_dma *r=(xx_parsec_dma *)xx_mem_alloc(sizeof(*r));
    if (r) xx_parsec_dma_init(r,d,base);
    return r;
}
void xx_parsec_dma_destroy(xx_parsec_dma *r) { xx_legacy_sound_driver_destroy(r); }
void xx_parsec_dma_free(xx_parsec_dma *r) {
    if (r) { xx_parsec_dma_destroy(r);xx_mem_free(r); }
}
