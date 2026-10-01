/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/dvitype.web
 * TeX DVI2 complete font/page/opcode/postamble grammar, exact backward page pointers, balanced stacks and declared font references. Original page programs and metadata exported; Japanese dialects, font lookup, specials execution and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_TEX_DVI_H
#define XX_TEX_DVI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tex_dvi { Abstractformat format; } xx_tex_dvi;
XXFC_API void xx_tex_dvi_init(xx_tex_dvi *,xx_io_device *,int64_t);
XXFC_API xx_tex_dvi *xx_tex_dvi_create(xx_io_device *,int64_t);
XXFC_API void xx_tex_dvi_destroy(xx_tex_dvi *);
XXFC_API void xx_tex_dvi_free(xx_tex_dvi *);
XXFC_API bool xx_tex_dvi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tex_dvi_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
