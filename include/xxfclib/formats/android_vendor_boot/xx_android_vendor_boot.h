/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Versions 3/4; exports ramdisk fragments, DTB and bootconfig without decompressing ramdisks.
 */
#ifndef XX_ANDROID_VENDOR_BOOT_H
#define XX_ANDROID_VENDOR_BOOT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_android_vendor_boot { Abstractformat format; } xx_android_vendor_boot;
XXFC_API void xx_android_vendor_boot_init(xx_android_vendor_boot *,xx_io_device *,int64_t);
XXFC_API xx_android_vendor_boot *xx_android_vendor_boot_create(xx_io_device *,int64_t);
XXFC_API void xx_android_vendor_boot_destroy(xx_android_vendor_boot *);
XXFC_API void xx_android_vendor_boot_free(xx_android_vendor_boot *);
XXFC_API bool xx_android_vendor_boot_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_android_vendor_boot_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
