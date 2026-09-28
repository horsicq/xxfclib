/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Exports bounded authentication/key/descriptor components. Does not verify signatures or establish trust.
 */
#ifndef XX_ANDROID_VBMETA_H
#define XX_ANDROID_VBMETA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_android_vbmeta { Abstractformat format; } xx_android_vbmeta;
XXFC_API void xx_android_vbmeta_init(xx_android_vbmeta *,xx_io_device *,int64_t);
XXFC_API xx_android_vbmeta *xx_android_vbmeta_create(xx_io_device *,int64_t);
XXFC_API void xx_android_vbmeta_destroy(xx_android_vbmeta *);
XXFC_API void xx_android_vbmeta_free(xx_android_vbmeta *);
XXFC_API bool xx_android_vbmeta_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_android_vbmeta_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
