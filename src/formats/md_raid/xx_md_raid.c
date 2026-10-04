/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/md_raid/xx_md_raid.h"
#include "xxfclib/memory/xx_memory.h"
void xx_md_raid_init(xx_md_raid *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_MD_RAID,"img"); }
xx_md_raid *xx_md_raid_create(xx_io_device *d,int64_t base) { xx_md_raid *r=(xx_md_raid *)xx_mem_alloc(sizeof(*r)); if(r) xx_md_raid_init(r,d,base); return r; }
void xx_md_raid_destroy(xx_md_raid *r) { xx_volume_destroy(r); }
void xx_md_raid_free(xx_md_raid *r) { if(r) { xx_md_raid_destroy(r); xx_mem_free(r); } }
