/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_APRICOT_PARTITIONS_READER_H
#define XX_APRICOT_PARTITIONS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_apricot_partitions;
XXFC_API void xx_apricot_partitions_init(xx_apricot_partitions *, xx_io_device *, int64_t);
XXFC_API xx_apricot_partitions *xx_apricot_partitions_create(xx_io_device *, int64_t);
XXFC_API void xx_apricot_partitions_destroy(xx_apricot_partitions *);
XXFC_API void xx_apricot_partitions_free(xx_apricot_partitions *);
static inline Abstractformat *xx_apricot_partitions_to_format(xx_apricot_partitions *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
