/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/ndstool/master/source/header.h
 * Nintendo DS unitcode 0 images with header/logo CRC16 checks. Exports ARM9/ARM7, FAT files and optional banner; validates Nitro FNT/overlay references. No secure-area decryption, DSi/modcrypt, relocation or execution.
 */
#ifndef XX_NINTENDO_NDS_H
#define XX_NINTENDO_NDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_nds { Abstractformat format; } xx_nintendo_nds;
XXFC_API void xx_nintendo_nds_init(xx_nintendo_nds *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_nds *xx_nintendo_nds_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_nds_destroy(xx_nintendo_nds *);
XXFC_API void xx_nintendo_nds_free(xx_nintendo_nds *);
XXFC_API bool xx_nintendo_nds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_nds_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
