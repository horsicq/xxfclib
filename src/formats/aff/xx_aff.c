/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/aff/xx_aff.h"
#include "xxfclib/memory/xx_memory.h"
void xx_aff_init(xx_aff *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_AFF,"aff"); }
xx_aff *xx_aff_create(xx_io_device *d,int64_t base) { xx_aff *r=(xx_aff *)xx_mem_alloc(sizeof(*r)); if(r) xx_aff_init(r,d,base); return r; }
void xx_aff_destroy(xx_aff *r) { xx_volume_destroy(r); }
void xx_aff_free(xx_aff *r) { if(r) { xx_aff_destroy(r); xx_mem_free(r); } }
