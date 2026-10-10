/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/vxfs/xx_vxfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_vxfs_init(xx_vxfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_VXFS, "img");
}
xx_vxfs *xx_vxfs_create(xx_io_device *d, int64_t base)
{
    xx_vxfs *r = (xx_vxfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_vxfs_init(r, d, base);
    return r;
}
void xx_vxfs_destroy(xx_vxfs *r)
{
    xx_volume_destroy(r);
}
void xx_vxfs_free(xx_vxfs *r)
{
    if (r) {
        xx_vxfs_destroy(r);
        xx_mem_free(r);
    }
}
