/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/coff_object/xx_coff_object.h"
#include "xxfclib/memory/xx_memory.h"
void xx_coff_object_init(xx_coff_object *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_COFF_OBJECT,"obj"); }
xx_coff_object *xx_coff_object_create(xx_io_device *d,int64_t base) { xx_coff_object *r=(xx_coff_object *)xx_mem_alloc(sizeof(*r)); if(r) xx_coff_object_init(r,d,base); return r; }
void xx_coff_object_destroy(xx_coff_object *r) { xx_volume_destroy(r); }
void xx_coff_object_free(xx_coff_object *r) { if(r) { xx_coff_object_destroy(r); xx_mem_free(r); } }
