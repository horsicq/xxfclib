/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://git.suckless.org/farbfeld/file/FORMAT.html
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_FARBFELD_H
#define XX_FARBFELD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_farbfeld { Abstractformat format; } xx_farbfeld;
XXFC_API void xx_farbfeld_init(xx_farbfeld *,xx_io_device *,int64_t);
XXFC_API xx_farbfeld *xx_farbfeld_create(xx_io_device *,int64_t);
XXFC_API void xx_farbfeld_destroy(xx_farbfeld *);
XXFC_API void xx_farbfeld_free(xx_farbfeld *);
XXFC_API bool xx_farbfeld_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_farbfeld_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
