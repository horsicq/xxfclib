/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/gfs2/xx_gfs2.h"
#include "xxfclib/memory/xx_memory.h"
void xx_gfs2_init(xx_gfs2 *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_GFS2,"img"); }
xx_gfs2 *xx_gfs2_create(xx_io_device *d,int64_t base) { xx_gfs2 *r=(xx_gfs2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_gfs2_init(r,d,base); return r; }
void xx_gfs2_destroy(xx_gfs2 *r) { xx_volume_destroy(r); }
void xx_gfs2_free(xx_gfs2 *r) { if(r) { xx_gfs2_destroy(r); xx_mem_free(r); } }
