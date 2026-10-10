/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/hpfs/xx_hpfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_hpfs_init(xx_hpfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_HPFS, "img");
}
xx_hpfs *xx_hpfs_create(xx_io_device *d, int64_t base)
{
    xx_hpfs *r = (xx_hpfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_hpfs_init(r, d, base);
    return r;
}
void xx_hpfs_destroy(xx_hpfs *r)
{
    xx_volume_destroy(r);
}
void xx_hpfs_free(xx_hpfs *r)
{
    if (r) {
        xx_hpfs_destroy(r);
        xx_mem_free(r);
    }
}
