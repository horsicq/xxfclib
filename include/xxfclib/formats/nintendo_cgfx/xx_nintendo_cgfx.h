/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Gericom/EveryFileExplorer/master/3DS/NintendoWare/GFX/CGFX.cs
 * Little-endian 3DS CGFX version0x05000000 with DATA and optional IMAG blocks. Validates sixteen dictionary counts, dictionary node/name/data references and block extents. Exports encoded DATA/IMAG components; object/patricia-tree semantics, graphics decoding, GPU commands and rendering unsupported.
 */
#ifndef XX_NINTENDO_CGFX_H
#define XX_NINTENDO_CGFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_cgfx { Abstractformat format; } xx_nintendo_cgfx;
XXFC_API void xx_nintendo_cgfx_init(xx_nintendo_cgfx *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_cgfx *xx_nintendo_cgfx_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_cgfx_destroy(xx_nintendo_cgfx *);
XXFC_API void xx_nintendo_cgfx_free(xx_nintendo_cgfx *);
XXFC_API bool xx_nintendo_cgfx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_cgfx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
