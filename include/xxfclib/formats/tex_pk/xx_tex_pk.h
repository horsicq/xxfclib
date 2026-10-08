/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/pktype.web
 * Four-byte-aligned TeX PK89 complete preamble, bounded short/extended/long glyph packets, fully decoded packed-number row/repeat framing and specials/post padding. Encoded original glyph packets exported; font rendering, external paths and unknown opcodes are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_TEX_PK_H
#define XX_TEX_PK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tex_pk { Abstractformat format; } xx_tex_pk;
XXFC_API void xx_tex_pk_init(xx_tex_pk *,xx_io_device *,int64_t);
XXFC_API xx_tex_pk *xx_tex_pk_create(xx_io_device *,int64_t);
XXFC_API void xx_tex_pk_destroy(xx_tex_pk *);
XXFC_API void xx_tex_pk_free(xx_tex_pk *);
XXFC_API bool xx_tex_pk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tex_pk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tex_pk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tex_pk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tex_pk_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
