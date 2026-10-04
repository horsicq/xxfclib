/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/te_executable/xx_te_executable.h"
#include "xxfclib/memory/xx_memory.h"
void xx_te_executable_init(xx_te_executable *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_TE_EXECUTABLE,"te"); }
xx_te_executable *xx_te_executable_create(xx_io_device *d,int64_t base) { xx_te_executable *r=(xx_te_executable *)xx_mem_alloc(sizeof(*r)); if(r) xx_te_executable_init(r,d,base); return r; }
void xx_te_executable_destroy(xx_te_executable *r) { xx_volume_destroy(r); }
void xx_te_executable_free(xx_te_executable *r) { if(r) { xx_te_executable_destroy(r); xx_mem_free(r); } }
