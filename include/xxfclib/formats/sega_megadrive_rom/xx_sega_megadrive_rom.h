/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/loadrom.c
 * Unwrapped big-endian Mega Drive ROMs with SEGA system header, RAM stack/reset vectors, declared start/end ROM range and16-bit body checksum.512 bytes-16MiB, even length. Exports vectors, cartridge header and original ROM body; SMD/interleaved dumps,32X/Sega CD, bank emulation and execution unsupported.
 */
#ifndef XX_SEGA_MEGADRIVE_ROM_H
#define XX_SEGA_MEGADRIVE_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_megadrive_rom { Abstractformat format; } xx_sega_megadrive_rom;
XXFC_API void xx_sega_megadrive_rom_init(xx_sega_megadrive_rom *,xx_io_device *,int64_t);
XXFC_API xx_sega_megadrive_rom *xx_sega_megadrive_rom_create(xx_io_device *,int64_t);
XXFC_API void xx_sega_megadrive_rom_destroy(xx_sega_megadrive_rom *);
XXFC_API void xx_sega_megadrive_rom_free(xx_sega_megadrive_rom *);
XXFC_API bool xx_sega_megadrive_rom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sega_megadrive_rom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
