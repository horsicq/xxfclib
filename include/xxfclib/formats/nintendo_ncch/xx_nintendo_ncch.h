/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/ncch.h
 * NCCH versions 0/1/2 with NoCrypto and 512-byte media units. Exports extended header, plain/logo/ExeFS/RomFS encoded sections. No RSA/hash trust verification, ExeFS decompression or RomFS traversal; encrypted/seed crypto rejected.
 */
#ifndef XX_NINTENDO_NCCH_H
#define XX_NINTENDO_NCCH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_ncch { Abstractformat format; } xx_nintendo_ncch;
XXFC_API void xx_nintendo_ncch_init(xx_nintendo_ncch *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_ncch *xx_nintendo_ncch_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_ncch_destroy(xx_nintendo_ncch *);
XXFC_API void xx_nintendo_ncch_free(xx_nintendo_ncch *);
XXFC_API bool xx_nintendo_ncch_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_ncch_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
