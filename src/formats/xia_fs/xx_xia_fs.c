/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/xia_fs/xx_xia_fs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_xia_fs_init(xx_xia_fs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_XIA_FS, "img");
}
xx_xia_fs *xx_xia_fs_create(xx_io_device *d, int64_t base)
{
    xx_xia_fs *r = (xx_xia_fs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_xia_fs_init(r, d, base);
    return r;
}
void xx_xia_fs_destroy(xx_xia_fs *r)
{
    xx_volume_destroy(r);
}
void xx_xia_fs_free(xx_xia_fs *r)
{
    if (r) {
        xx_xia_fs_destroy(r);
        xx_mem_free(r);
    }
}
