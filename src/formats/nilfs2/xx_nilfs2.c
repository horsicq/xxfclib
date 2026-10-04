/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/nilfs2/xx_nilfs2.h"
#include "xxfclib/memory/xx_memory.h"
void xx_nilfs2_init(xx_nilfs2 *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_NILFS2,"img"); }
xx_nilfs2 *xx_nilfs2_create(xx_io_device *d,int64_t base) { xx_nilfs2 *r=(xx_nilfs2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_nilfs2_init(r,d,base); return r; }
void xx_nilfs2_destroy(xx_nilfs2 *r) { xx_volume_destroy(r); }
void xx_nilfs2_free(xx_nilfs2 *r) { if(r) { xx_nilfs2_destroy(r); xx_mem_free(r); } }
