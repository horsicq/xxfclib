/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/locus_fs/xx_locus_fs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_locus_fs_init(xx_locus_fs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_LOCUS_FS,"img"); }
xx_locus_fs *xx_locus_fs_create(xx_io_device *d,int64_t base) { xx_locus_fs *r=(xx_locus_fs *)xx_mem_alloc(sizeof(*r)); if(r) xx_locus_fs_init(r,d,base); return r; }
void xx_locus_fs_destroy(xx_locus_fs *r) { xx_volume_destroy(r); }
void xx_locus_fs_free(xx_locus_fs *r) { if(r) { xx_locus_fs_destroy(r); xx_mem_free(r); } }
