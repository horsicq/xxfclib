/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/zfs/xx_zfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_zfs_init(xx_zfs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_ZFS,"img"); }
xx_zfs *xx_zfs_create(xx_io_device *d,int64_t base) { xx_zfs *r=(xx_zfs *)xx_mem_alloc(sizeof(*r)); if(r) xx_zfs_init(r,d,base); return r; }
void xx_zfs_destroy(xx_zfs *r) { xx_volume_destroy(r); }
void xx_zfs_free(xx_zfs *r) { if(r) { xx_zfs_destroy(r); xx_mem_free(r); } }
