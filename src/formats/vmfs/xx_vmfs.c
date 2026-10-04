/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/vmfs/xx_vmfs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_vmfs_init(xx_vmfs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_VMFS,"img"); }
xx_vmfs *xx_vmfs_create(xx_io_device *d,int64_t base) { xx_vmfs *r=(xx_vmfs *)xx_mem_alloc(sizeof(*r)); if(r) xx_vmfs_init(r,d,base); return r; }
void xx_vmfs_destroy(xx_vmfs *r) { xx_volume_destroy(r); }
void xx_vmfs_free(xx_vmfs *r) { if(r) { xx_vmfs_destroy(r); xx_mem_free(r); } }
