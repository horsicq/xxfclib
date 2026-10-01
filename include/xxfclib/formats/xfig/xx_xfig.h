/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://fossies.org/linux/xfig/doc/FORMAT3.2
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_XFIG_H
#define XX_XFIG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xfig { Abstractformat format; } xx_xfig;
XXFC_API void xx_xfig_init(xx_xfig *,xx_io_device *,int64_t);
XXFC_API xx_xfig *xx_xfig_create(xx_io_device *,int64_t);
XXFC_API void xx_xfig_destroy(xx_xfig *);
XXFC_API void xx_xfig_free(xx_xfig *);
XXFC_API bool xx_xfig_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xfig_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
