/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_ZX_SPECTRUM_PZX_H
#define XX_ZX_SPECTRUM_PZX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_zx_spectrum_pzx { Abstractformat format; } xx_zx_spectrum_pzx;
XXFC_API void xx_zx_spectrum_pzx_init(xx_zx_spectrum_pzx *,xx_io_device *,int64_t);
XXFC_API xx_zx_spectrum_pzx *xx_zx_spectrum_pzx_create(xx_io_device *,int64_t);
XXFC_API void xx_zx_spectrum_pzx_destroy(xx_zx_spectrum_pzx *);
XXFC_API void xx_zx_spectrum_pzx_free(xx_zx_spectrum_pzx *);
XXFC_API bool xx_zx_spectrum_pzx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_zx_spectrum_pzx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
