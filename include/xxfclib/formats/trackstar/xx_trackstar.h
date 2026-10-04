/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Trackstar APP reader. No payload is executed.
 */
#ifndef XX_TRACKSTAR_H
#define XX_TRACKSTAR_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_trackstar;
typedef xx_trackstar xx_trackstar_t;
XXFC_API void xx_trackstar_init(xx_trackstar *,xx_io_device *,int64_t);
XXFC_API xx_trackstar *xx_trackstar_create(xx_io_device *,int64_t);
XXFC_API void xx_trackstar_destroy(xx_trackstar *);
XXFC_API void xx_trackstar_free(xx_trackstar *);
XXFC_API bool xx_trackstar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_trackstar_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_trackstar_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_trackstar_to_format(xx_trackstar *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

