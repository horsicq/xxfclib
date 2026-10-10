/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/hp_lif/xx_hp_lif.h"
#include "xxfclib/memory/xx_memory.h"
void xx_hp_lif_init(xx_hp_lif *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_HP_LIF, "img");
}
xx_hp_lif *xx_hp_lif_create(xx_io_device *d, int64_t base)
{
    xx_hp_lif *r = (xx_hp_lif *)xx_mem_alloc(sizeof(*r));
    if (r) xx_hp_lif_init(r, d, base);
    return r;
}
void xx_hp_lif_destroy(xx_hp_lif *r)
{
    xx_volume_destroy(r);
}
void xx_hp_lif_free(xx_hp_lif *r)
{
    if (r) {
        xx_hp_lif_destroy(r);
        xx_mem_free(r);
    }
}
