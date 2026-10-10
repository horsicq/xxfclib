/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/linux_ext/xx_linux_ext.h"
#include "xxfclib/memory/xx_memory.h"
void xx_linux_ext_init(xx_linux_ext *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_LINUX_EXT, "img");
}
xx_linux_ext *xx_linux_ext_create(xx_io_device *d, int64_t base)
{
    xx_linux_ext *r = (xx_linux_ext *)xx_mem_alloc(sizeof(*r));
    if (r) xx_linux_ext_init(r, d, base);
    return r;
}
void xx_linux_ext_destroy(xx_linux_ext *r)
{
    xx_volume_destroy(r);
}
void xx_linux_ext_free(xx_linux_ext *r)
{
    if (r) {
        xx_linux_ext_destroy(r);
        xx_mem_free(r);
    }
}
