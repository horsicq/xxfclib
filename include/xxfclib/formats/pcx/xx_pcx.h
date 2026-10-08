/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://files.mpoli.fi/unpacked/software/programm/general/gcgpe10.zip/pcx.txt
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_PCX_H
#define XX_PCX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pcx { Abstractformat format; } xx_pcx;
XXFC_API void xx_pcx_init(xx_pcx *,xx_io_device *,int64_t);
XXFC_API xx_pcx *xx_pcx_create(xx_io_device *,int64_t);
XXFC_API void xx_pcx_destroy(xx_pcx *);
XXFC_API void xx_pcx_free(xx_pcx *);
XXFC_API bool xx_pcx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pcx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pcx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pcx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pcx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
