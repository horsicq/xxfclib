/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/amiga_pfs/xx_amiga_pfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_amiga_pfs_init(xx_amiga_pfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_AMIGA_PFS, "img");
}
xx_amiga_pfs *xx_amiga_pfs_create(xx_io_device *d, int64_t base)
{
    xx_amiga_pfs *r = (xx_amiga_pfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amiga_pfs_init(r, d, base);
    return r;
}
void xx_amiga_pfs_destroy(xx_amiga_pfs *r)
{
    xx_volume_destroy(r);
}
void xx_amiga_pfs_free(xx_amiga_pfs *r)
{
    if (r) {
        xx_amiga_pfs_destroy(r);
        xx_mem_free(r);
    }
}
