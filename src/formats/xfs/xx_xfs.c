/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/xfs/xx_xfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_xfs_init(xx_xfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_XFS, "img");
}
xx_xfs *xx_xfs_create(xx_io_device *d, int64_t base)
{
    xx_xfs *r = (xx_xfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_xfs_init(r, d, base);
    return r;
}
void xx_xfs_destroy(xx_xfs *r)
{
    xx_volume_destroy(r);
}
void xx_xfs_free(xx_xfs *r)
{
    if (r) {
        xx_xfs_destroy(r);
        xx_mem_free(r);
    }
}
