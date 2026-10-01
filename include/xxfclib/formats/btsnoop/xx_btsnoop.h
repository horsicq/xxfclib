/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BTSNOOP_H
#define XX_BTSNOOP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_btsnoop { Abstractformat format; } xx_btsnoop;
XXFC_API void xx_btsnoop_init(xx_btsnoop *,xx_io_device *,int64_t);
XXFC_API xx_btsnoop *xx_btsnoop_create(xx_io_device *,int64_t);
XXFC_API void xx_btsnoop_destroy(xx_btsnoop *);
XXFC_API void xx_btsnoop_free(xx_btsnoop *);
XXFC_API bool xx_btsnoop_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_btsnoop_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
