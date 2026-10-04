/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/human68k_partitions/xx_human68k_partitions.h"
#include "xxfclib/memory/xx_memory.h"
void xx_human68k_partitions_init(xx_human68k_partitions *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_HUMAN68K_PARTITIONS,"img"); }
xx_human68k_partitions *xx_human68k_partitions_create(xx_io_device *d,int64_t base) { xx_human68k_partitions *r=(xx_human68k_partitions *)xx_mem_alloc(sizeof(*r)); if(r) xx_human68k_partitions_init(r,d,base); return r; }
void xx_human68k_partitions_destroy(xx_human68k_partitions *r) { xx_volume_destroy(r); }
void xx_human68k_partitions_free(xx_human68k_partitions *r) { if(r) { xx_human68k_partitions_destroy(r); xx_mem_free(r); } }
