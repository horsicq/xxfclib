/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/xenix_partitions/xx_xenix_partitions.h"
#include "xxfclib/memory/xx_memory.h"
void xx_xenix_partitions_init(xx_xenix_partitions *r,xx_io_device *d,int64_t base) { xx_volume_init(r,d,base,XX_FILE_TYPE_XENIX_PARTITIONS,"img"); }
xx_xenix_partitions *xx_xenix_partitions_create(xx_io_device *d,int64_t base) { xx_xenix_partitions *r=(xx_xenix_partitions *)xx_mem_alloc(sizeof(*r)); if(r) xx_xenix_partitions_init(r,d,base); return r; }
void xx_xenix_partitions_destroy(xx_xenix_partitions *r) { xx_volume_destroy(r); }
void xx_xenix_partitions_free(xx_xenix_partitions *r) { if(r) { xx_xenix_partitions_destroy(r); xx_mem_free(r); } }
