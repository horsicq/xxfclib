/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/windows_ldm/xx_windows_ldm.h"
#include "xxfclib/memory/xx_memory.h"
void xx_windows_ldm_init(xx_windows_ldm *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_WINDOWS_LDM,"img"); }
xx_windows_ldm *xx_windows_ldm_create(xx_io_device *d,int64_t base) { xx_windows_ldm *r=(xx_windows_ldm *)xx_mem_alloc(sizeof(*r)); if(r) xx_windows_ldm_init(r,d,base); return r; }
void xx_windows_ldm_destroy(xx_windows_ldm *r) { xx_volume_destroy(r); }
void xx_windows_ldm_free(xx_windows_ldm *r) { if(r) { xx_windows_ldm_destroy(r); xx_mem_free(r); } }
