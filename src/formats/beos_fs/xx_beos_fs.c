/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/beos_fs/xx_beos_fs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_beos_fs_init(xx_beos_fs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_BEOS_FS, "img");
}
xx_beos_fs *xx_beos_fs_create(xx_io_device *d, int64_t base)
{
    xx_beos_fs *r = (xx_beos_fs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_beos_fs_init(r, d, base);
    return r;
}
void xx_beos_fs_destroy(xx_beos_fs *r)
{
    xx_volume_destroy(r);
}
void xx_beos_fs_free(xx_beos_fs *r)
{
    if (r) {
        xx_beos_fs_destroy(r);
        xx_mem_free(r);
    }
}
