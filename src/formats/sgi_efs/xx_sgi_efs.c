/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/sgi_efs/xx_sgi_efs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_sgi_efs_init(xx_sgi_efs *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_SGI_EFS,"img"); }
xx_sgi_efs *xx_sgi_efs_create(xx_io_device *d,int64_t base) { xx_sgi_efs *r=(xx_sgi_efs *)xx_mem_alloc(sizeof(*r)); if(r) xx_sgi_efs_init(r,d,base); return r; }
void xx_sgi_efs_destroy(xx_sgi_efs *r) { xx_volume_destroy(r); }
void xx_sgi_efs_free(xx_sgi_efs *r) { if(r) { xx_sgi_efs_destroy(r); xx_mem_free(r); } }
