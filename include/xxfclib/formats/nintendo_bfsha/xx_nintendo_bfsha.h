/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/KillzXGaming/BfshaLibrary/master/ShaderLibrary/WiiU/BfshaSaverWiiU.cs
 * WiiU big-endian FSHA2.0 archives with up to32 shader models and256 simple programs each (4096 output components maximum), no options/attributes/samplers/uniform metadata, standard GX2 vertex/pixel headers. Validates signed relative pointers, dictionary records, at most4096 string-pool records, archive backlinks, header/data separation and aligned code extents. Exports model/program/GX2 headers and stored shader code; overlapping/shared payloads, geometry shaders, complex bindings, other versions, Switch relocation, GPU execution and rendering unsupported.
 */
#ifndef XX_NINTENDO_BFSHA_H
#define XX_NINTENDO_BFSHA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bfsha { Abstractformat format; } xx_nintendo_bfsha;
XXFC_API void xx_nintendo_bfsha_init(xx_nintendo_bfsha *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bfsha *xx_nintendo_bfsha_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bfsha_destroy(xx_nintendo_bfsha *);
XXFC_API void xx_nintendo_bfsha_free(xx_nintendo_bfsha *);
XXFC_API bool xx_nintendo_bfsha_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bfsha_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
