/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/f2fs/xx_f2fs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_f2fs_init(xx_f2fs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_F2FS,"img"); }
xx_f2fs *xx_f2fs_create(xx_io_device *d,int64_t base) { xx_f2fs *r=(xx_f2fs *)xx_mem_alloc(sizeof(*r)); if(r) xx_f2fs_init(r,d,base); return r; }
void xx_f2fs_destroy(xx_f2fs *r) { xx_volume_destroy(r); }
void xx_f2fs_free(xx_f2fs *r) { if(r) { xx_f2fs_destroy(r); xx_mem_free(r); } }
