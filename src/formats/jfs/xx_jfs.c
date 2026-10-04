/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/jfs/xx_jfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_jfs_init(xx_jfs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_JFS,"img"); }
xx_jfs *xx_jfs_create(xx_io_device *d,int64_t base) { xx_jfs *r=(xx_jfs *)xx_mem_alloc(sizeof(*r)); if(r) xx_jfs_init(r,d,base); return r; }
void xx_jfs_destroy(xx_jfs *r) { xx_volume_destroy(r); }
void xx_jfs_free(xx_jfs *r) { if(r) { xx_jfs_destroy(r); xx_mem_free(r); } }
