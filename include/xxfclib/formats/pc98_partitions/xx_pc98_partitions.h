/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_PC98_PARTITIONS_READER_H
#define XX_PC98_PARTITIONS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_pc98_partitions;
XXFC_API void xx_pc98_partitions_init(xx_pc98_partitions *, xx_io_device *, int64_t);
XXFC_API xx_pc98_partitions *xx_pc98_partitions_create(xx_io_device *, int64_t);
XXFC_API void xx_pc98_partitions_destroy(xx_pc98_partitions *);
XXFC_API void xx_pc98_partitions_free(xx_pc98_partitions *);
static inline Abstractformat *xx_pc98_partitions_to_format(xx_pc98_partitions *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
