/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/megastep/makeself/blob/master/makeself-header.sh
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_MAKESELF_H
#define XX_MAKESELF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_makeself { Abstractformat format; } xx_makeself;
XXFC_API void xx_makeself_init(xx_makeself *,xx_io_device *,int64_t);
XXFC_API xx_makeself *xx_makeself_create(xx_io_device *,int64_t);
XXFC_API void xx_makeself_destroy(xx_makeself *);
XXFC_API void xx_makeself_free(xx_makeself *);
XXFC_API bool xx_makeself_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_makeself_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
