/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * MSX AB-header ROM and FE-header binary images.
 */
#ifndef XX_MSX_ROM_H
#define XX_MSX_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_msx_image_kind_e {
    XX_MSX_IMAGE_UNKNOWN = 0,
    XX_MSX_IMAGE_ROM = 1,
    XX_MSX_IMAGE_BINARY = 2
} xx_msx_image_kind_t;
typedef struct xx_msx_rom {
    Abstractformat format;
    xx_msx_image_kind_t image_kind;
    uint16_t start_address;
    uint16_t end_address;
    uint16_t init_address;
    uint16_t runtime_address;
    uint16_t device_address;
    uint16_t text_address;
    uint16_t header_size;
} xx_msx_rom;
XXFC_API void xx_msx_rom_init(xx_msx_rom *, xx_io_device *, int64_t);
XXFC_API xx_msx_rom *xx_msx_rom_create(xx_io_device *, int64_t);
XXFC_API void xx_msx_rom_destroy(xx_msx_rom *);
XXFC_API void xx_msx_rom_free(xx_msx_rom *);
XXFC_API bool xx_msx_rom_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_msx_rom_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_msx_rom_get_memory_map(Abstractformat *, xx_memory_map_mode_t, xx_memory_map *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
