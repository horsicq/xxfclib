/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_PUTTY_PPK_H
#define XX_PUTTY_PPK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_putty_ppk {Abstractformat format;} xx_putty_ppk;
XXFC_API void xx_putty_ppk_init(xx_putty_ppk *,xx_io_device *,int64_t);
XXFC_API xx_putty_ppk *xx_putty_ppk_create(xx_io_device *,int64_t);
XXFC_API void xx_putty_ppk_destroy(xx_putty_ppk *);
XXFC_API void xx_putty_ppk_free(xx_putty_ppk *);
XXFC_API bool xx_putty_ppk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_putty_ppk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
