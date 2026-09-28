/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_ACORN_UEF_H
#define XX_ACORN_UEF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_acorn_uef { Abstractformat format; } xx_acorn_uef;
XXFC_API void xx_acorn_uef_init(xx_acorn_uef *,xx_io_device *,int64_t);
XXFC_API xx_acorn_uef *xx_acorn_uef_create(xx_io_device *,int64_t);
XXFC_API void xx_acorn_uef_destroy(xx_acorn_uef *);
XXFC_API void xx_acorn_uef_free(xx_acorn_uef *);
XXFC_API bool xx_acorn_uef_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_acorn_uef_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
