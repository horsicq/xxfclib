/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/loadrom.c
 * Unwrapped big-endian Mega Drive ROMs with SEGA system header, RAM stack/reset vectors and declared start/end ROM range.512 bytes-16MiB, even
 * length. Exports vectors, cartridge header and original ROM body; SMD/interleaved dumps,32X/Sega CD, bank emulation and execution unsupported.
 */
#ifndef XX_SEGA_MEGADRIVE_ROM_H
#define XX_SEGA_MEGADRIVE_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sega_megadrive_rom {
    Abstractformat format;
} xx_sega_megadrive_rom;

#define XX_SEGA_MEGADRIVE_ROM_VECTOR_COUNT 64U
#define XX_SEGA_MEGADRIVE_ROM_DEFAULT_MIN_STRING_LENGTH 10U
#define XX_SEGA_MEGADRIVE_ROM_MAX_STRING_LENGTH 4096U

/** Parsed, bounded Mega Drive cartridge header and exception/IRQ vectors. */
typedef struct xx_sega_megadrive_rom_info_s {
    char copyright[33];
    char domestic_name[49];
    char overseas_name[49];
    char product_code[15];
    char peripherals[17];
    char sram_code[13];
    char modem_code[13];
    char country_code[17];
    uint16_t checksum;
    uint32_t rom_start;
    uint32_t rom_end;
    uint32_t ram_start;
    uint32_t ram_end;
    uint32_t vectors[XX_SEGA_MEGADRIVE_ROM_VECTOR_COUNT];
    uint64_t image_size;
} xx_sega_megadrive_rom_info;

/** One printable string selected by the SMD plugin's uppercase filter. */
typedef struct xx_sega_megadrive_rom_string_s {
    uint64_t offset;      /**< Absolute byte offset in the supplied device. */
    uint32_t ordinal;     /**< Ordinal before the uppercase filter is applied. */
    uint32_t length;      /**< Unicode scalar count after trailing-space trim. */
    uint32_t storage_size; /**< Source bytes including a consumed terminator. */
    const char *text;     /**< UTF-8/display text, valid only during the callback. */
} xx_sega_megadrive_rom_string;

typedef bool (*xx_sega_megadrive_rom_string_callback)(const xx_sega_megadrive_rom_string *record, void *user);

XXFC_API void xx_sega_megadrive_rom_init(xx_sega_megadrive_rom *, xx_io_device *, int64_t);
XXFC_API xx_sega_megadrive_rom *xx_sega_megadrive_rom_create(xx_io_device *, int64_t);
XXFC_API void xx_sega_megadrive_rom_destroy(xx_sega_megadrive_rom *);
XXFC_API void xx_sega_megadrive_rom_free(xx_sega_megadrive_rom *);
XXFC_API bool xx_sega_megadrive_rom_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sega_megadrive_rom_handle_base_info(Abstractformat *, xx_pd_struct *);

/* Parse the complete strict reader first, then expose its header and vectors.
 * Text fields are right-trimmed and NUL terminated. The device cursor is
 * restored on success and failure. */
XXFC_API bool xx_sega_megadrive_rom_read_info(Abstractformat *format, xx_sega_megadrive_rom_info *info, xx_pd_struct *pd);
XXFC_API const char *xx_sega_megadrive_rom_vector_name(uint32_t index);

/* Compatibility probe used by Radare2's SMD plugin: an image larger than
 * 0x190 bytes with "SEGA" at offset 0x100. This deliberately remains
 * separate from the strict checksum/vector validator. */
XXFC_API bool xx_sega_megadrive_rom_has_compatible_header(xx_io_device *device, int64_t base_address, uint64_t image_size);

/* Scan a compatible image for the SMD plugin's uppercase strings. The scan is
 * bounded to a 16 MiB cartridge image and 4096 scalars per candidate. A zero
 * minimum selects 10. Returning false from callback stops successfully. */
XXFC_API bool xx_sega_megadrive_rom_visit_uppercase_strings(xx_io_device *device, int64_t base_address, uint64_t image_size, uint32_t minimum_length,
                                                            xx_sega_megadrive_rom_string_callback callback, void *user, xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sega_megadrive_rom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sega_megadrive_rom_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sega_megadrive_rom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sega_megadrive_rom_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sega_megadrive_rom_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
