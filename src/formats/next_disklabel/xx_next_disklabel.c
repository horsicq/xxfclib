/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/next_disklabel/xx_next_disklabel.h"
#include "xxfclib/memory/xx_memory.h"
void xx_next_disklabel_init(xx_next_disklabel *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_NEXT_DISKLABEL,"img"); }
xx_next_disklabel *xx_next_disklabel_create(xx_io_device *d,int64_t base) { xx_next_disklabel *r=(xx_next_disklabel *)xx_mem_alloc(sizeof(*r)); if(r) xx_next_disklabel_init(r,d,base); return r; }
void xx_next_disklabel_destroy(xx_next_disklabel *r) { xx_volume_destroy(r); }
void xx_next_disklabel_free(xx_next_disklabel *r) { if(r) { xx_next_disklabel_destroy(r); xx_mem_free(r); } }
