/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_XBOX360_LAYOUT_READER_H
#define XX_XBOX360_LAYOUT_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_xbox360_layout;
XXFC_API void xx_xbox360_layout_init(xx_xbox360_layout *,xx_io_device *,int64_t);
XXFC_API xx_xbox360_layout *xx_xbox360_layout_create(xx_io_device *,int64_t);
XXFC_API void xx_xbox360_layout_destroy(xx_xbox360_layout *);
XXFC_API void xx_xbox360_layout_free(xx_xbox360_layout *);
static inline Abstractformat *xx_xbox360_layout_to_format(xx_xbox360_layout *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
