/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gbdev/pandocs/master/src/The_Cartridge_Header.md
 * GB/CGB cartridges with full48-byte boot logo, declared ROM-size/bank table, header checksum and global16-bit checksum. Supports normal size codes0-8
 * and52-54,32KiB-8MiB. Exports original16KiB banks; mapper emulation, copier wrappers, malformed/incomplete dumps and execution unsupported.
 */
#ifndef XX_NINTENDO_GB_ROM_H
#define XX_NINTENDO_GB_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_gb_rom {
    Abstractformat format;
} xx_nintendo_gb_rom;
XXFC_API void xx_nintendo_gb_rom_init(xx_nintendo_gb_rom *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_gb_rom *xx_nintendo_gb_rom_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_gb_rom_destroy(xx_nintendo_gb_rom *);
XXFC_API void xx_nintendo_gb_rom_free(xx_nintendo_gb_rom *);
XXFC_API bool xx_nintendo_gb_rom_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_gb_rom_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_gb_rom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_gb_rom_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_gb_rom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_gb_rom_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_gb_rom_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
