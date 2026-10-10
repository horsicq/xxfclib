/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_UNIX_BFS_READER_H
#define XX_UNIX_BFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_unix_bfs;
XXFC_API void xx_unix_bfs_init(xx_unix_bfs *, xx_io_device *, int64_t);
XXFC_API xx_unix_bfs *xx_unix_bfs_create(xx_io_device *, int64_t);
XXFC_API void xx_unix_bfs_destroy(xx_unix_bfs *);
XXFC_API void xx_unix_bfs_free(xx_unix_bfs *);
static inline Abstractformat *xx_unix_bfs_to_format(xx_unix_bfs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
