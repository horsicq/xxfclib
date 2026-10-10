/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_MINIX_PARTITIONS_READER_H
#define XX_MINIX_PARTITIONS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_minix_partitions;
XXFC_API void xx_minix_partitions_init(xx_minix_partitions *, xx_io_device *, int64_t);
XXFC_API xx_minix_partitions *xx_minix_partitions_create(xx_io_device *, int64_t);
XXFC_API void xx_minix_partitions_destroy(xx_minix_partitions *);
XXFC_API void xx_minix_partitions_free(xx_minix_partitions *);
static inline Abstractformat *xx_minix_partitions_to_format(xx_minix_partitions *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
