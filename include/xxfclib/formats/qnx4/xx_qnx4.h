/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_QNX4_READER_H
#define XX_QNX4_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_qnx4;
XXFC_API void xx_qnx4_init(xx_qnx4 *,xx_io_device *,int64_t);
XXFC_API xx_qnx4 *xx_qnx4_create(xx_io_device *,int64_t);
XXFC_API void xx_qnx4_destroy(xx_qnx4 *);
XXFC_API void xx_qnx4_free(xx_qnx4 *);
static inline Abstractformat *xx_qnx4_to_format(xx_qnx4 *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
