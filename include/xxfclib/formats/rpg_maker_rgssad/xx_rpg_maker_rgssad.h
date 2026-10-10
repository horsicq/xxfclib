/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rpg_maker_rgssad.h @brief RPG Maker RGSSAD archive reader. */

#ifndef XXFCLIB_FORMAT_RPG_MAKER_RGSSAD_H
#define XXFCLIB_FORMAT_RPG_MAKER_RGSSAD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RPG Maker (RGSS) resource archive: Game.rgssad (XP),
 * Game.rgss2a (VX) and Game.rgss3a (VX Ace).
 *
 * Every field is LITTLE endian.  Header:
 *   0x00  "RGSSAD\0"
 *   0x07  u8 version: 1 (XP and VX) or 3 (VX Ace)
 *
 * Key stream: next(k) = k * 7 + 3 (mod 2^32).
 *
 * Version 1 (entries run from offset 8 to EOF, data inline):
 *   the key starts at 0xDEADCAFE and advances after every field it touches:
 *     u32  name length  ^ key, then advance
 *     u8[] name         each byte ^ (key & 0xFF), advancing after each byte
 *     u32  data size    ^ key, then advance
 *     u8[] data         encrypted with the key as it now stands
 *
 * Version 3 (a table, then the data):
 *   0x08  u32 seed; the table key is seed * 9 + 3
 *   0x0C  entries of four u32 fields, each ^ table key:
 *           data offset (0 ends the table), data size, data key, name length
 *         followed by the name, byte i ^ (table key >> 8 * (i % 4))
 *
 * Data: byte i of a member is XORed with byte (i % 4) of the current data
 * key (little-endian order); the key advances after every 4 bytes.
 *
 * Names use '\\' as the directory separator and are UTF-8 (VX Ace) or
 * Shift-JIS (XP).  A name that is valid UTF-8 is kept byte for byte with
 * '\\' turned into '/'; otherwise every byte >= 0x80 and '%' become "%XX".
 * A repeated name (ASCII case-insensitive) gets "%_<entry index>" before its
 * extension; unsafe names (absolute, drive, "..", Windows device names,
 * control or reserved characters) are listed but refused on extraction.
 *
 * Acceptance: the header, at least one entry, every name 1..1024 bytes with
 * no control byte after decryption, every member inside the file; version 1
 * entries must end exactly at EOF; version 3 needs its terminator and every
 * member must start at or after the table's end.
 */
typedef struct xx_rpg_maker_rgssad {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t rgss_version; /**< 1 or 3 once parsed, else 0. */
} xx_rpg_maker_rgssad;

typedef xx_rpg_maker_rgssad xx_rpg_maker_rgssad_t;

XXFC_API void xx_rpg_maker_rgssad_init(xx_rpg_maker_rgssad *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_rpg_maker_rgssad *xx_rpg_maker_rgssad_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rpg_maker_rgssad_destroy(xx_rpg_maker_rgssad *archive);
XXFC_API void xx_rpg_maker_rgssad_free(xx_rpg_maker_rgssad *archive);

XXFC_API bool xx_rpg_maker_rgssad_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rpg_maker_rgssad_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rpg_maker_rgssad_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_rpg_maker_rgssad_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rpg_maker_rgssad_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rpg_maker_rgssad_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rpg_maker_rgssad_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rpg_maker_rgssad_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rpg_maker_rgssad_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RPG_MAKER_RGSSAD_H */
