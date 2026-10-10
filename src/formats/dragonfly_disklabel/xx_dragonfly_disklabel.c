/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/dragonfly_disklabel/xx_dragonfly_disklabel.h"
#include "xxfclib/memory/xx_memory.h"
void xx_dragonfly_disklabel_init(xx_dragonfly_disklabel *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_DRAGONFLY_DISKLABEL, "img");
}
xx_dragonfly_disklabel *xx_dragonfly_disklabel_create(xx_io_device *d, int64_t base)
{
    xx_dragonfly_disklabel *r = (xx_dragonfly_disklabel *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dragonfly_disklabel_init(r, d, base);
    return r;
}
void xx_dragonfly_disklabel_destroy(xx_dragonfly_disklabel *r)
{
    xx_volume_destroy(r);
}
void xx_dragonfly_disklabel_free(xx_dragonfly_disklabel *r)
{
    if (r) {
        xx_dragonfly_disklabel_destroy(r);
        xx_mem_free(r);
    }
}
