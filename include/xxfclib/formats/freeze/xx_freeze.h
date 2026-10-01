/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FREEZE_H
#define XX_FREEZE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_freeze { Abstractformat format; } xx_freeze;
XXFC_API void xx_freeze_init(xx_freeze *,xx_io_device *,int64_t);
XXFC_API xx_freeze *xx_freeze_create(xx_io_device *,int64_t);
XXFC_API void xx_freeze_destroy(xx_freeze *);
XXFC_API void xx_freeze_free(xx_freeze *);
XXFC_API bool xx_freeze_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_freeze_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
