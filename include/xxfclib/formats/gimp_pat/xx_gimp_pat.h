/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://developer.gimp.org/core/standards/pat/
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_GIMP_PAT_H
#define XX_GIMP_PAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gimp_pat { Abstractformat format; } xx_gimp_pat;
XXFC_API void xx_gimp_pat_init(xx_gimp_pat *,xx_io_device *,int64_t);
XXFC_API xx_gimp_pat *xx_gimp_pat_create(xx_io_device *,int64_t);
XXFC_API void xx_gimp_pat_destroy(xx_gimp_pat *);
XXFC_API void xx_gimp_pat_free(xx_gimp_pat *);
XXFC_API bool xx_gimp_pat_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gimp_pat_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
