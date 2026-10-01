/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BZIP1_H
#define XX_BZIP1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bzip1 { Abstractformat format; } xx_bzip1;
XXFC_API void xx_bzip1_init(xx_bzip1 *,xx_io_device *,int64_t);
XXFC_API xx_bzip1 *xx_bzip1_create(xx_io_device *,int64_t);
XXFC_API void xx_bzip1_destroy(xx_bzip1 *);
XXFC_API void xx_bzip1_free(xx_bzip1 *);
XXFC_API bool xx_bzip1_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bzip1_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
