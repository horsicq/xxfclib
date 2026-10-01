/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/ncsd.h
 * Full-extent NCSD gamecards with up to eight unencrypted NCCH partitions. Exports whole declared partition components; NAND filesystem partitions and encrypted NCCH rejected. No cartridge signature verification.
 */
#ifndef XX_NINTENDO_NCSD_H
#define XX_NINTENDO_NCSD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_ncsd { Abstractformat format; } xx_nintendo_ncsd;
XXFC_API void xx_nintendo_ncsd_init(xx_nintendo_ncsd *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_ncsd *xx_nintendo_ncsd_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_ncsd_destroy(xx_nintendo_ncsd *);
XXFC_API void xx_nintendo_ncsd_free(xx_nintendo_ncsd *);
XXFC_API bool xx_nintendo_ncsd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_ncsd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
