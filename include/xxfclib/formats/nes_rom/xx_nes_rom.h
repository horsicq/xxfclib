/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://github.com/libretro/libretro-fceumm/blob/master/src/ines.c
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_NES_ROM_H
#define XX_NES_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nes_rom { Abstractformat format; } xx_nes_rom;
XXFC_API void xx_nes_rom_init(xx_nes_rom *,xx_io_device *,int64_t);
XXFC_API xx_nes_rom *xx_nes_rom_create(xx_io_device *,int64_t);
XXFC_API void xx_nes_rom_destroy(xx_nes_rom *);
XXFC_API void xx_nes_rom_free(xx_nes_rom *);
XXFC_API bool xx_nes_rom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nes_rom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
