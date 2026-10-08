/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://gota7.github.io/Citric-Composer/specs/common.html
 * NintendoWare endian-aware bounded section extraction only. Exports complete encoded STRG/INFO/FILE blocks; individual nested sounds/external files are not resolved.
 */
#ifndef XX_NINTENDO_BFSAR_H
#define XX_NINTENDO_BFSAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bfsar { Abstractformat format; } xx_nintendo_bfsar;
XXFC_API void xx_nintendo_bfsar_init(xx_nintendo_bfsar *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bfsar *xx_nintendo_bfsar_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bfsar_destroy(xx_nintendo_bfsar *);
XXFC_API void xx_nintendo_bfsar_free(xx_nintendo_bfsar *);
XXFC_API bool xx_nintendo_bfsar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bfsar_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bfsar_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bfsar_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bfsar_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
