/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/aliaspixdec.c
 * Alias/Wavefront PIX8/24-bit positive geometry and complete per-row RLE packets with exact physical EOF. Original encoded scanlines exported; no pixel/color conversion. Signatureless offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_ALIAS_PIX_H
#define XX_ALIAS_PIX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alias_pix {Abstractformat format;} xx_alias_pix;
XXFC_API void xx_alias_pix_init(xx_alias_pix *,xx_io_device *,int64_t);
XXFC_API xx_alias_pix *xx_alias_pix_create(xx_io_device *,int64_t);
XXFC_API void xx_alias_pix_destroy(xx_alias_pix *);
XXFC_API void xx_alias_pix_free(xx_alias_pix *);
XXFC_API bool xx_alias_pix_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alias_pix_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
