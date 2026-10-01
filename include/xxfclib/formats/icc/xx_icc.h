/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.color.org/specification/ICC.1-2022-05.pdf
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_ICC_H
#define XX_ICC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_icc { Abstractformat format; } xx_icc;
XXFC_API void xx_icc_init(xx_icc *,xx_io_device *,int64_t);
XXFC_API xx_icc *xx_icc_create(xx_io_device *,int64_t);
XXFC_API void xx_icc_destroy(xx_icc *);
XXFC_API void xx_icc_free(xx_icc *);
XXFC_API bool xx_icc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_icc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
