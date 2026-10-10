/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/lvm2/xx_lvm2.h"
#include "xxfclib/memory/xx_memory.h"
void xx_lvm2_init(xx_lvm2 *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_LVM2, "lvm");
}
xx_lvm2 *xx_lvm2_create(xx_io_device *d, int64_t base)
{
    xx_lvm2 *r = (xx_lvm2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lvm2_init(r, d, base);
    return r;
}
void xx_lvm2_destroy(xx_lvm2 *r)
{
    xx_volume_destroy(r);
}
void xx_lvm2_free(xx_lvm2 *r)
{
    if (r) {
        xx_lvm2_destroy(r);
        xx_mem_free(r);
    }
}
