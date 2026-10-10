/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_NILFS2_READER_H
#define XX_NILFS2_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_nilfs2;
XXFC_API void xx_nilfs2_init(xx_nilfs2 *, xx_io_device *, int64_t);
XXFC_API xx_nilfs2 *xx_nilfs2_create(xx_io_device *, int64_t);
XXFC_API void xx_nilfs2_destroy(xx_nilfs2 *);
XXFC_API void xx_nilfs2_free(xx_nilfs2 *);
static inline Abstractformat *xx_nilfs2_to_format(xx_nilfs2 *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
