/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LPAK_H
#define XX_LPAK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lpak { Abstractformat format; } xx_lpak;
XXFC_API void xx_lpak_init(xx_lpak *,xx_io_device *,int64_t);
XXFC_API xx_lpak *xx_lpak_create(xx_io_device *,int64_t);
XXFC_API void xx_lpak_destroy(xx_lpak *);
XXFC_API void xx_lpak_free(xx_lpak *);
XXFC_API bool xx_lpak_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lpak_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
