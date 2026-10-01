/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/tex.web
 * Standard TeX TFM with exact12-count framing, typed character/metric/ligature/kern/recipe references and finite fixed-point tables. Original metric sections exported; Japanese/JFM dialects, font lookup and rendering are unsupported. Signatureless detection is offset-zero only.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_TEX_TFM_H
#define XX_TEX_TFM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tex_tfm { Abstractformat format; } xx_tex_tfm;
XXFC_API void xx_tex_tfm_init(xx_tex_tfm *,xx_io_device *,int64_t);
XXFC_API xx_tex_tfm *xx_tex_tfm_create(xx_io_device *,int64_t);
XXFC_API void xx_tex_tfm_destroy(xx_tex_tfm *);
XXFC_API void xx_tex_tfm_free(xx_tex_tfm *);
XXFC_API bool xx_tex_tfm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tex_tfm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
