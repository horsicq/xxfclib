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
typedef struct xx_nes_rom {
    Abstractformat format;
} xx_nes_rom;
XXFC_API void xx_nes_rom_init(xx_nes_rom *, xx_io_device *, int64_t);
XXFC_API xx_nes_rom *xx_nes_rom_create(xx_io_device *, int64_t);
XXFC_API void xx_nes_rom_destroy(xx_nes_rom *);
XXFC_API void xx_nes_rom_free(xx_nes_rom *);
XXFC_API bool xx_nes_rom_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nes_rom_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nes_rom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nes_rom_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nes_rom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nes_rom_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nes_rom_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
