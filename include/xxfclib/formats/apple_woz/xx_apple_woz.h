/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded WOZ1 5.25-inch and WOZ2 5.25-/3.5-inch reader, including INFO v3
 * FLUX mappings. Members are the exact stored bitstreams or flux-timing
 * bytes and metadata tables; protected tracks are not sector-decoded.
 */
#ifndef XX_APPLE_WOZ_H
#define XX_APPLE_WOZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apple_woz { Abstractformat format; } xx_apple_woz;
XXFC_API void xx_apple_woz_init(xx_apple_woz *,xx_io_device *,int64_t);
XXFC_API xx_apple_woz *xx_apple_woz_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_woz_destroy(xx_apple_woz *);
XXFC_API void xx_apple_woz_free(xx_apple_woz *);
XXFC_API bool xx_apple_woz_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_woz_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
