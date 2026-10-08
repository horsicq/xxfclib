/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_TGA_H
#define XX_TGA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tga { Abstractformat format; } xx_tga;
XXFC_API void xx_tga_init(xx_tga *,xx_io_device *,int64_t);
XXFC_API xx_tga *xx_tga_create(xx_io_device *,int64_t);
XXFC_API void xx_tga_destroy(xx_tga *);
XXFC_API void xx_tga_free(xx_tga *);
XXFC_API bool xx_tga_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tga_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tga_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tga_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tga_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
