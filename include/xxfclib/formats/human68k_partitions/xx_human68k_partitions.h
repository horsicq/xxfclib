/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HUMAN68K_PARTITIONS_READER_H
#define XX_HUMAN68K_PARTITIONS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_human68k_partitions;
XXFC_API void xx_human68k_partitions_init(xx_human68k_partitions *, xx_io_device *, int64_t);
XXFC_API xx_human68k_partitions *xx_human68k_partitions_create(xx_io_device *, int64_t);
XXFC_API void xx_human68k_partitions_destroy(xx_human68k_partitions *);
XXFC_API void xx_human68k_partitions_free(xx_human68k_partitions *);
static inline Abstractformat *xx_human68k_partitions_to_format(xx_human68k_partitions *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
