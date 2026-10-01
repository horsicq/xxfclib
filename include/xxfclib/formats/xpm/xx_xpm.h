/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.xfree86.org/current/xpm.pdf
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_XPM_H
#define XX_XPM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xpm { Abstractformat format; } xx_xpm;
XXFC_API void xx_xpm_init(xx_xpm *,xx_io_device *,int64_t);
XXFC_API xx_xpm *xx_xpm_create(xx_io_device *,int64_t);
XXFC_API void xx_xpm_destroy(xx_xpm *);
XXFC_API void xx_xpm_free(xx_xpm *);
XXFC_API bool xx_xpm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xpm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
