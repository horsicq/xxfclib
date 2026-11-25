/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Sega Master System and Game Gear TMR SEGA cartridge images.
 */
#ifndef XX_SEGA_SMS_ROM_H
#define XX_SEGA_SMS_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_sms_rom {
    Abstractformat format;
    uint32_t header_offset;
    uint16_t checksum;
    uint32_t product_code;
    uint8_t version;
    uint8_t region;
    uint32_t declared_rom_size;
} xx_sega_sms_rom;
XXFC_API void xx_sega_sms_rom_init(xx_sega_sms_rom *, xx_io_device *, int64_t);
XXFC_API xx_sega_sms_rom *xx_sega_sms_rom_create(xx_io_device *, int64_t);
XXFC_API void xx_sega_sms_rom_destroy(xx_sega_sms_rom *);
XXFC_API void xx_sega_sms_rom_free(xx_sega_sms_rom *);
/** Check the four standard cartridge-header slots without changing the device cursor. */
XXFC_API bool xx_sega_sms_rom_probe_device(xx_io_device *, int64_t);
XXFC_API bool xx_sega_sms_rom_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sega_sms_rom_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sega_sms_rom_get_memory_map(Abstractformat *, xx_memory_map_mode_t, xx_memory_map *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
