/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/hpofs/xx_hpofs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_hpofs_init(xx_hpofs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_HPOFS,"img"); }
xx_hpofs *xx_hpofs_create(xx_io_device *d,int64_t base) { xx_hpofs *r=(xx_hpofs *)xx_mem_alloc(sizeof(*r)); if(r) xx_hpofs_init(r,d,base); return r; }
void xx_hpofs_destroy(xx_hpofs *r) { xx_volume_destroy(r); }
void xx_hpofs_free(xx_hpofs *r) { if(r) { xx_hpofs_destroy(r); xx_mem_free(r); } }
