/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dxa.h @brief DxLib DXA resource archive reader (incl. .wolf). */

#ifndef XXFCLIB_FORMAT_DXA_H
#define XXFCLIB_FORMAT_DXA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DxLib engine archive (*.dxa, and the Wolf RPG Editor *.wolf
 * files of the DxLib era, plus .hud/.usi/.med/.dat/.bin/.bcx renames).
 *
 * Every field is LITTLE endian.  The whole file is XOR-ed with a 12-byte key
 * derived from the game's password, so the header is not readable as is:
 *
 *   versions 1..4 (32-bit fields), header 0x18 bytes (0x1C from version 4)
 *     0x00  u16 'DX' (0x5844)       0x02  u16 version
 *     0x04  u32 index size          0x08  u32 data start (= header size)
 *     0x0C  u32 index offset        0x10  u32 file table (index relative)
 *     0x14  u32 directory table     0x18  u32 code page (version 4)
 *     The key runs over absolute file positions (key[pos % 12]) for the
 *     header, the member data and the index alike.
 *
 *   version 6 (64-bit fields), header 0x30 bytes
 *     0x00  u16 'DX'  0x02 u16 6   0x04 u32 index size
 *     0x08  u64 data start (0x30)  0x10 u64 index offset
 *     0x18  u64 file table         0x20 u64 directory table
 *     0x28  u32 code page          0x2C u32 flags
 *     The header is keyed from position 0, the index from its own start,
 *     and every member from key position (unpacked size % 12).
 *
 * The index is a name table, a file table and a directory table, in that
 * order.  A name entry is {u16 units, u16 parity, upper-case name, name},
 * both names padded to units * 4 bytes.  A file head (0x28 bytes in version
 * 1, 0x2C in 2..4, 0x40 in 6) holds the name offset, attributes (0x10 =
 * directory), three FILETIMEs, the data offset (a directory-table offset for
 * a directory), the size and, from version 2, the packed size (-1 = stored).
 * A directory entry is {own file head, parent directory (-1 for the root),
 * child count, first child file head}.  Packed members use DxLib's LZ:
 * u32 unpacked size, u32 packed size (with this 9-byte header), u8 escape
 * code, then literals and escape-introduced matches of 4..8195 bytes at
 * distances up to 16 MiB.
 *
 * The key is never stored.  It is recovered the way GARbro's DxOpener
 * guesses it, from the header bytes whose plain value is known: 'DX', the
 * version and the data start in every version, the zero high dwords of the
 * version 6 header, and for versions 1..4 the index size, taken as running
 * from the index offset to EOF (DxLib writes the index last).  Versions 5,
 * 7 (SHA-256 per-file keys) and 8 (Huffman index) are not supported.
 *
 * There is no plain magic, so recognition is structural: the recovered key
 * must turn the header into ranges that fit, the root directory entry must
 * have parent -1, every child directory must name its parent, every name
 * must be a terminated string inside the name table, and every member must
 * lie inside the file.  Garbage fails on the first 0x30 bytes.
 *
 * Member names: components are joined with '/'; names that are not valid
 * UTF-8 (Shift-JIS, the usual code page) get every byte >= 0x80 written as
 * "%XX", and '%', '/' and '\\' inside a component are escaped the same way;
 * a repeated path (compared case-insensitively) gets "%_<index>" before its
 * extension; an unsafe path ('.'/'..', control bytes, Windows device names,
 * : < > | ? * ") is listed but refused on extraction.
 */
typedef struct xx_dxa {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t dxa_version;
    uint32_t index_size;
    int64_t index_offset; /**< Relative to the archive start. */
    int64_t data_start;
    uint8_t key[12];
} xx_dxa;

typedef xx_dxa xx_dxa_t;

/** XX_META_ID_COMPRESSION_METHOD values. */
#define XX_DXA_METHOD_STORE 0U
#define XX_DXA_METHOD_LZ 1U

XXFC_API void xx_dxa_init(xx_dxa *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_dxa *xx_dxa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dxa_destroy(xx_dxa *archive);
XXFC_API void xx_dxa_free(xx_dxa *archive);

XXFC_API bool xx_dxa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dxa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dxa_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_dxa_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dxa_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dxa_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dxa_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dxa_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dxa_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DXA_H */
