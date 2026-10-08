/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ModernMAK/Relic-Game-Tool/main/src/relic/chunky/chunky/header.py
 * Relic Chunky3.1 headers and bounded FOLD/DATA hierarchy, up to4096 chunks/depth32 with28-byte chunk headers. Exports named encoded DATA components; other revisions, typed asset decoding, rendering and game execution unsupported.
 */
#ifndef XX_RELIC_CHUNKY_H
#define XX_RELIC_CHUNKY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_relic_chunky { Abstractformat format; } xx_relic_chunky;
XXFC_API void xx_relic_chunky_init(xx_relic_chunky *,xx_io_device *,int64_t);
XXFC_API xx_relic_chunky *xx_relic_chunky_create(xx_io_device *,int64_t);
XXFC_API void xx_relic_chunky_destroy(xx_relic_chunky *);
XXFC_API void xx_relic_chunky_free(xx_relic_chunky *);
XXFC_API bool xx_relic_chunky_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_relic_chunky_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_relic_chunky_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_relic_chunky_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_relic_chunky_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
