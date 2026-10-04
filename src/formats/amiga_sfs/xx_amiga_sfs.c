/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/amiga_sfs/xx_amiga_sfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_amiga_sfs_init(xx_amiga_sfs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_AMIGA_SFS,"img"); }
xx_amiga_sfs *xx_amiga_sfs_create(xx_io_device *d,int64_t base) { xx_amiga_sfs *r=(xx_amiga_sfs *)xx_mem_alloc(sizeof(*r)); if(r) xx_amiga_sfs_init(r,d,base); return r; }
void xx_amiga_sfs_destroy(xx_amiga_sfs *r) { xx_volume_destroy(r); }
void xx_amiga_sfs_free(xx_amiga_sfs *r) { if(r) { xx_amiga_sfs_destroy(r); xx_mem_free(r); } }
