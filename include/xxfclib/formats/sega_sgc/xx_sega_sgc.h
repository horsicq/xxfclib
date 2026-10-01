/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_SEGA_SGC_H
#define XX_SEGA_SGC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_sgc { Abstractformat format; } xx_sega_sgc;
XXFC_API void xx_sega_sgc_init(xx_sega_sgc *,xx_io_device *,int64_t);
XXFC_API xx_sega_sgc *xx_sega_sgc_create(xx_io_device *,int64_t);
XXFC_API void xx_sega_sgc_destroy(xx_sega_sgc *);
XXFC_API void xx_sega_sgc_free(xx_sega_sgc *);
XXFC_API bool xx_sega_sgc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sega_sgc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
