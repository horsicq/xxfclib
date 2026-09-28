/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Version 0, stored DTB/DTBO entries; no overlay application or newer compressed entries.
 */
#ifndef XX_ANDROID_DTBO_H
#define XX_ANDROID_DTBO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_android_dtbo { Abstractformat format; } xx_android_dtbo;
XXFC_API void xx_android_dtbo_init(xx_android_dtbo *,xx_io_device *,int64_t);
XXFC_API xx_android_dtbo *xx_android_dtbo_create(xx_io_device *,int64_t);
XXFC_API void xx_android_dtbo_destroy(xx_android_dtbo *);
XXFC_API void xx_android_dtbo_free(xx_android_dtbo *);
XXFC_API bool xx_android_dtbo_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_android_dtbo_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
