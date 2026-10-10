/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_MD_RAID_READER_H
#define XX_MD_RAID_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_md_raid;
XXFC_API void xx_md_raid_init(xx_md_raid *, xx_io_device *, int64_t);
XXFC_API xx_md_raid *xx_md_raid_create(xx_io_device *, int64_t);
XXFC_API void xx_md_raid_destroy(xx_md_raid *);
XXFC_API void xx_md_raid_free(xx_md_raid *);
static inline Abstractformat *xx_md_raid_to_format(xx_md_raid *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
