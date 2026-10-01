/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ZX_SPECTRUM_TZX_H
#define XX_ZX_SPECTRUM_TZX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_zx_spectrum_tzx { Abstractformat format; } xx_zx_spectrum_tzx;
XXFC_API void xx_zx_spectrum_tzx_init(xx_zx_spectrum_tzx *,xx_io_device *,int64_t);
XXFC_API xx_zx_spectrum_tzx *xx_zx_spectrum_tzx_create(xx_io_device *,int64_t);
XXFC_API void xx_zx_spectrum_tzx_destroy(xx_zx_spectrum_tzx *);
XXFC_API void xx_zx_spectrum_tzx_free(xx_zx_spectrum_tzx *);
XXFC_API bool xx_zx_spectrum_tzx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_zx_spectrum_tzx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
