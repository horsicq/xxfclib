/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded WOZ1 5.25-inch and WOZ2 5.25-/3.5-inch reader, including INFO v3
 * FLUX mappings. Members are the exact stored bitstreams or flux-timing
 * bytes and metadata tables. Standard Apple II 13-/16-sector GCR bitstreams
 * additionally expose checksum-verified sectors and native filesystem files.
 * Flux-only tracks and Macintosh GCR remain preserved components.
 */
#ifndef XX_APPLE_WOZ_H
#define XX_APPLE_WOZ_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_apple_woz;
XXFC_API void xx_apple_woz_init(xx_apple_woz *,xx_io_device *,int64_t);
XXFC_API xx_apple_woz *xx_apple_woz_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_woz_destroy(xx_apple_woz *);
XXFC_API void xx_apple_woz_free(xx_apple_woz *);
XXFC_API bool xx_apple_woz_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_woz_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_woz_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
