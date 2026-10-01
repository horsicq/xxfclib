/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_crt.h @brief Commodore cartridge image (.crt) reader. */

#ifndef XXFCLIB_FORMAT_CRT_H
#define XXFCLIB_FORMAT_CRT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A VICE cartridge image for any of the five Commodore machines.
 *
 * Layout (VICE manual, "The CRT cartridge image format"), all big endian:
 *
 *   0x00  char[16] signature, space padded:
 *                  "C64 CARTRIDGE   "  "C128 CARTRIDGE  "  "CBM2 CARTRIDGE  "
 *                  "VIC20 CARTRIDGE "  "PLUS4 CARTRIDGE "
 *   0x10  u32  header length (0x40; a few old C64 files say 0x20 but still
 *              carry the full 0x40 bytes)
 *   0x14  u8,u8 version major/minor (1.0, 1.1 for C64, 2.0 for the others)
 *   0x16  u16  hardware (cartridge) type, machine specific
 *   0x18  u8   EXROM line, 0x19 u8 GAME line (C64 only)
 *   0x1A  u8   hardware sub type (version >= 1.1)
 *   0x20  char[32] cartridge name, NUL padded
 *
 * Then CHIP packets until the end of the image:
 *
 *   +0x00 "CHIP"
 *   +0x04 u32  packet length including this 16-byte header
 *   +0x08 u16  chip type: 0 ROM, 1 RAM (no data), 2 flash, 3 EEPROM
 *   +0x0A u16  bank number
 *   +0x0C u16  load address
 *   +0x0E u16  image size in bytes
 *   +0x10 the image data; the packet may be padded past it
 *
 * Each chip image with data is one member, stored verbatim.
 */

enum {
    XX_CRT_MACHINE_C64 = 0,
    XX_CRT_MACHINE_C128 = 1,
    XX_CRT_MACHINE_CBM2 = 2,
    XX_CRT_MACHINE_VIC20 = 3,
    XX_CRT_MACHINE_PLUS4 = 4
};

/** Upper bound on CHIP packets walked (GMod3 16 MiB is 2048 x 8 KiB). */
#define XX_CRT_MAX_CHIPS 65536U

typedef struct xx_crt {
    Abstractformat format;
    int machine;              /**< XX_CRT_MACHINE_* */
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t hardware_type;
    uint8_t exrom;
    uint8_t game;
    uint8_t subtype;
    char name[33];            /**< Printable ASCII copy of the name field. */
    uint32_t header_length;   /**< As stored. */
    uint64_t number_of_chips; /**< Every CHIP packet walked. */
    uint64_t number_of_records;
} xx_crt;

typedef xx_crt xx_crt_t;

XXFC_API void xx_crt_init(xx_crt *crt, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_crt *xx_crt_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_crt_destroy(xx_crt *crt);
XXFC_API void xx_crt_free(xx_crt *crt);

XXFC_API bool xx_crt_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_crt_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_crt_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_crt_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_crt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_crt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_crt_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_crt_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_crt_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CRT_H */
