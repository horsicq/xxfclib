/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/dec_disklabel/xx_dec_disklabel.h"
#include "xxfclib/memory/xx_memory.h"
void xx_dec_disklabel_init(xx_dec_disklabel *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_DEC_DISKLABEL,"img"); }
xx_dec_disklabel *xx_dec_disklabel_create(xx_io_device *d,int64_t base) { xx_dec_disklabel *r=(xx_dec_disklabel *)xx_mem_alloc(sizeof(*r)); if(r) xx_dec_disklabel_init(r,d,base); return r; }
void xx_dec_disklabel_destroy(xx_dec_disklabel *r) { xx_volume_destroy(r); }
void xx_dec_disklabel_free(xx_dec_disklabel *r) { if(r) { xx_dec_disklabel_destroy(r); xx_mem_free(r); } }
