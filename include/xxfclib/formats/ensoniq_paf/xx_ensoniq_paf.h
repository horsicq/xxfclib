/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Ensoniq PARIS Audio File stored-component reader.
 */
#ifndef XX_ENSONIQ_PAF_H
#define XX_ENSONIQ_PAF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ensoniq_paf { Abstractformat format; } xx_ensoniq_paf;
XXFC_API void xx_ensoniq_paf_init(xx_ensoniq_paf *,xx_io_device *,int64_t);
XXFC_API xx_ensoniq_paf *xx_ensoniq_paf_create(xx_io_device *,int64_t);
XXFC_API void xx_ensoniq_paf_destroy(xx_ensoniq_paf *);
XXFC_API void xx_ensoniq_paf_free(xx_ensoniq_paf *);
XXFC_API bool xx_ensoniq_paf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ensoniq_paf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
