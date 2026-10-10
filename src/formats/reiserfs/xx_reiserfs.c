/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/reiserfs/xx_reiserfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_reiserfs_init(xx_reiserfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_REISERFS, "img");
}
xx_reiserfs *xx_reiserfs_create(xx_io_device *d, int64_t base)
{
    xx_reiserfs *r = (xx_reiserfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_reiserfs_init(r, d, base);
    return r;
}
void xx_reiserfs_destroy(xx_reiserfs *r)
{
    xx_volume_destroy(r);
}
void xx_reiserfs_free(xx_reiserfs *r)
{
    if (r) {
        xx_reiserfs_destroy(r);
        xx_mem_free(r);
    }
}
