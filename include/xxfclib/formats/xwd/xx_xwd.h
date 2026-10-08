/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/NetBSD/xsrc/trunk/external/mit/xorgproto/dist/include/X11/XWDFile.h
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_XWD_H
#define XX_XWD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xwd { Abstractformat format; } xx_xwd;
XXFC_API void xx_xwd_init(xx_xwd *,xx_io_device *,int64_t);
XXFC_API xx_xwd *xx_xwd_create(xx_io_device *,int64_t);
XXFC_API void xx_xwd_destroy(xx_xwd *);
XXFC_API void xx_xwd_free(xx_xwd *);
XXFC_API bool xx_xwd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xwd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xwd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xwd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xwd_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
