/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xp3.h @brief KiriKiri (TVP) XP3 resource archive reader. */

#ifndef XXFCLIB_FORMAT_XP3_H
#define XXFCLIB_FORMAT_XP3_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A KiriKiri / KiriKiri Z ".xp3" archive.  All integers little endian;
 * every offset is relative to the first byte of the signature.
 *
 *   0x00  u8[11] "XP3" 0D 0A 20 0A 1A 8B 67 01
 *   0x0B  u64    index position
 *
 * Version 2 archives point 0x0B at 0x17 and keep a u32 minor version (1) at
 * 0x13; the "index" at 0x17 is then a continuation record
 *
 *   +0 u8 0x80, +1 u64 0, +9 u64 position of the real index
 *
 * The index itself:
 *
 *   +0 u8 method: 0 stored, 1 zlib (RFC 1950)
 *   stored: +1 u64 size,                  +9 the index bytes
 *   zlib:   +1 u64 packed, +9 u64 size,  +17 the packed index
 *
 * The (unpacked) index is a run of chunks {char[4] tag, u64 size, data}.
 * "File" chunks describe one member each through sub-chunks of the same
 * shape; other top-level chunks (name maps and such) are skipped.
 *
 *   "info"  u32 flags (bit 31: encrypted), u64 original size,
 *           u64 packed size, u16 name length in UTF-16 units, UTF-16LE name
 *   "segm"  n * 28 bytes: u32 flags (bits 0..2: 0 stored, 1 zlib),
 *           u64 offset, u64 original size, u64 packed size
 *   "adlr"  u32 Adler-32 of the member
 *   others ("time", ...) are skipped
 *
 * A member is the concatenation of its segments.  Encrypted members are
 * extracted as stored (their per-game cipher is not undone) and flagged.
 * Names use '/' (a '\\' is converted); unsafe names are refused on
 * extraction and duplicate names get a "%_N" suffix.
 */
typedef struct xx_xp3 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t index_offset; /**< Absolute offset of the index record. */
    int64_t index_size;   /**< Unpacked index bytes. */
    bool index_packed;
    uint8_t version; /**< 1, or 2 with the continuation record. */
} xx_xp3;

typedef xx_xp3 xx_xp3_t;

XXFC_API void xx_xp3_init(xx_xp3 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_xp3 *xx_xp3_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xp3_destroy(xx_xp3 *archive);
XXFC_API void xx_xp3_free(xx_xp3 *archive);

XXFC_API bool xx_xp3_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xp3_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xp3_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_xp3_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xp3_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xp3_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xp3_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xp3_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xp3_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XP3_H */
