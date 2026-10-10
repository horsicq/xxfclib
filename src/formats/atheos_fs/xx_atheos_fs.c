/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/atheos_fs/xx_atheos_fs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_atheos_fs_init(xx_atheos_fs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_ATHEOS_FS, "img");
}
xx_atheos_fs *xx_atheos_fs_create(xx_io_device *d, int64_t base)
{
    xx_atheos_fs *r = (xx_atheos_fs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_atheos_fs_init(r, d, base);
    return r;
}
void xx_atheos_fs_destroy(xx_atheos_fs *r)
{
    xx_volume_destroy(r);
}
void xx_atheos_fs_free(xx_atheos_fs *r)
{
    if (r) {
        xx_atheos_fs_destroy(r);
        xx_mem_free(r);
    }
}
