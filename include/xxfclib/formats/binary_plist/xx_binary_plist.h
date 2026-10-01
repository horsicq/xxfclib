/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_BINARY_PLIST_H
#define XX_BINARY_PLIST_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_binary_plist { Abstractformat format; } xx_binary_plist;
XXFC_API void xx_binary_plist_init(xx_binary_plist *,xx_io_device *,int64_t);
XXFC_API xx_binary_plist *xx_binary_plist_create(xx_io_device *,int64_t);
XXFC_API void xx_binary_plist_destroy(xx_binary_plist *);
XXFC_API void xx_binary_plist_free(xx_binary_plist *);
XXFC_API bool xx_binary_plist_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_binary_plist_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
