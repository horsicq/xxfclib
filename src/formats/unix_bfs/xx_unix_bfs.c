/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/unix_bfs/xx_unix_bfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_unix_bfs_init(xx_unix_bfs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_UNIX_BFS, "img");
}
xx_unix_bfs *xx_unix_bfs_create(xx_io_device *d, int64_t base)
{
    xx_unix_bfs *r = (xx_unix_bfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unix_bfs_init(r, d, base);
    return r;
}
void xx_unix_bfs_destroy(xx_unix_bfs *r)
{
    xx_volume_destroy(r);
}
void xx_unix_bfs_free(xx_unix_bfs *r)
{
    if (r) {
        xx_unix_bfs_destroy(r);
        xx_mem_free(r);
    }
}
