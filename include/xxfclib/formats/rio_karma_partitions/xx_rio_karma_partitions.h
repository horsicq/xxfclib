/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_RIO_KARMA_PARTITIONS_READER_H
#define XX_RIO_KARMA_PARTITIONS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_rio_karma_partitions;
XXFC_API void xx_rio_karma_partitions_init(xx_rio_karma_partitions *,xx_io_device *,int64_t);
XXFC_API xx_rio_karma_partitions *xx_rio_karma_partitions_create(xx_io_device *,int64_t);
XXFC_API void xx_rio_karma_partitions_destroy(xx_rio_karma_partitions *);
XXFC_API void xx_rio_karma_partitions_free(xx_rio_karma_partitions *);
static inline Abstractformat *xx_rio_karma_partitions_to_format(xx_rio_karma_partitions *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
