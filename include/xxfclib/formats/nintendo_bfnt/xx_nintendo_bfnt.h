/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ObsidianX/3dstools/master/bffnt.py
 * FFNT version0x4000000, big-endian Wii U FFNT. Exports FINF/CWDH/CMAP metadata, TGLP header and up to32 encoded sheets; validates linked tables, glyph indices and disjoint framing. No texture conversion, BNTX sheets, text shaping or font rendering.
 */
#ifndef XX_NINTENDO_BFNT_H
#define XX_NINTENDO_BFNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bfnt { Abstractformat format; } xx_nintendo_bfnt;
XXFC_API void xx_nintendo_bfnt_init(xx_nintendo_bfnt *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bfnt *xx_nintendo_bfnt_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bfnt_destroy(xx_nintendo_bfnt *);
XXFC_API void xx_nintendo_bfnt_free(xx_nintendo_bfnt *);
XXFC_API bool xx_nintendo_bfnt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bfnt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bfnt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bfnt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bfnt_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
