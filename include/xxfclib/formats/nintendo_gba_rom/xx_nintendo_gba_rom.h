/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/gba-tools/master/src/gbafix.c
 * Standard GBA ROM headers with complete boot-logo CRC32, fixed byte96, reserved fields, header complement and bounded ARM branch entry. Power-of-two image sizes256 bytes-32MiB. Exports header and original body; multiboot/debug headers, copier wrappers, emulation and execution unsupported.
 */
#ifndef XX_NINTENDO_GBA_ROM_H
#define XX_NINTENDO_GBA_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_gba_rom { Abstractformat format; } xx_nintendo_gba_rom;
XXFC_API void xx_nintendo_gba_rom_init(xx_nintendo_gba_rom *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_gba_rom *xx_nintendo_gba_rom_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_gba_rom_destroy(xx_nintendo_gba_rom *);
XXFC_API void xx_nintendo_gba_rom_free(xx_nintendo_gba_rom *);
XXFC_API bool xx_nintendo_gba_rom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_gba_rom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_gba_rom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_gba_rom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_gba_rom_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
