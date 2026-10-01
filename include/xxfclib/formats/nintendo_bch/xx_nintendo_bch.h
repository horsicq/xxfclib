/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gdkchan/SPICA/master/SPICA/Formats/CtrH3D/H3DHeader.cs
 * 3DS BCH compatibility0x21 with six bounded nonoverlapping graphics sections and up to4096 checked relocation entries. Exports encoded contents/string/command/raw/relocation sections; runtime-initialized files, older compatibility levels, pointer fixups, GPU decoding and rendering unsupported. Uninitialized-storage counts are preserved as metadata and never allocated.
 */
#ifndef XX_NINTENDO_BCH_H
#define XX_NINTENDO_BCH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bch { Abstractformat format; } xx_nintendo_bch;
XXFC_API void xx_nintendo_bch_init(xx_nintendo_bch *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bch *xx_nintendo_bch_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bch_destroy(xx_nintendo_bch *);
XXFC_API void xx_nintendo_bch_free(xx_nintendo_bch *);
XXFC_API bool xx_nintendo_bch_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bch_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
