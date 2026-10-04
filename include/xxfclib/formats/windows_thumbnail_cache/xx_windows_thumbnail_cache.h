/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_WINDOWS_THUMBNAIL_CACHE_H
#define XX_WINDOWS_THUMBNAIL_CACHE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Windows CMMM thumbnail caches, versions 20/21/26/28/30/31/32.
 * Exports exact JPEG/PNG/BMP payloads, or .bin for other stored encodings.
 * Empty/unused entries are skipped. Names are cache hashes plus entry offsets,
 * since these caches do not store the original filesystem path. Header CRC-64
 * and Windows' sampled payload CRC-64 are verified; every payload byte is read.
 * Legacy OLE Thumbs.db files are handled by the compound-file reader. */
typedef struct xx_windows_thumbnail_cache { Abstractformat format; void *index; uint32_t version, cache_type; } xx_windows_thumbnail_cache;
XXFC_API void xx_windows_thumbnail_cache_init(xx_windows_thumbnail_cache *, xx_io_device *, int64_t);
XXFC_API xx_windows_thumbnail_cache *xx_windows_thumbnail_cache_create(xx_io_device *, int64_t);
XXFC_API void xx_windows_thumbnail_cache_destroy(xx_windows_thumbnail_cache *);
XXFC_API void xx_windows_thumbnail_cache_free(xx_windows_thumbnail_cache *);
#ifdef __cplusplus
}
#endif
#endif
