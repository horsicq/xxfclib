/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/freedesktop-unofficial-mirror/xorg__lib__libXcursor/master/src/file.c
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_XCURSOR_H
#define XX_XCURSOR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xcursor { Abstractformat format; } xx_xcursor;
XXFC_API void xx_xcursor_init(xx_xcursor *,xx_io_device *,int64_t);
XXFC_API xx_xcursor *xx_xcursor_create(xx_io_device *,int64_t);
XXFC_API void xx_xcursor_destroy(xx_xcursor *);
XXFC_API void xx_xcursor_free(xx_xcursor *);
XXFC_API bool xx_xcursor_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xcursor_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xcursor_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xcursor_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xcursor_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
