/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/atari_ahdi/xx_atari_ahdi.h"
#include "xxfclib/memory/xx_memory.h"
void xx_atari_ahdi_init(xx_atari_ahdi *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_ATARI_AHDI, "img");
}
xx_atari_ahdi *xx_atari_ahdi_create(xx_io_device *d, int64_t base)
{
    xx_atari_ahdi *r = (xx_atari_ahdi *)xx_mem_alloc(sizeof(*r));
    if (r) xx_atari_ahdi_init(r, d, base);
    return r;
}
void xx_atari_ahdi_destroy(xx_atari_ahdi *r)
{
    xx_volume_destroy(r);
}
void xx_atari_ahdi_free(xx_atari_ahdi *r)
{
    if (r) {
        xx_atari_ahdi_destroy(r);
        xx_mem_free(r);
    }
}
