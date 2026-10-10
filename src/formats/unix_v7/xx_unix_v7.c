/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/unix_v7/xx_unix_v7.h"
#include "xxfclib/memory/xx_memory.h"
void xx_unix_v7_init(xx_unix_v7 *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_UNIX_V7, "img");
}
xx_unix_v7 *xx_unix_v7_create(xx_io_device *d, int64_t base)
{
    xx_unix_v7 *r = (xx_unix_v7 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unix_v7_init(r, d, base);
    return r;
}
void xx_unix_v7_destroy(xx_unix_v7 *r)
{
    xx_volume_destroy(r);
}
void xx_unix_v7_free(xx_unix_v7 *r)
{
    if (r) {
        xx_unix_v7_destroy(r);
        xx_mem_free(r);
    }
}
