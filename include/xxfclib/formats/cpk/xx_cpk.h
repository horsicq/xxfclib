/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cpk.h @brief CRI Middleware CPK resource archive reader. */

#ifndef XXFCLIB_FORMAT_CPK_H
#define XXFCLIB_FORMAT_CPK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CRI Middleware CPK archive (CpkMaker / CRI File System).
 *
 * The file is a set of "packets".  Each packet is
 *
 *   +0x00  char[4]  signature: "CPK " (at file offset 0), "TOC ", "ITOC",
 *                   "ETOC", "GTOC"
 *   +0x04  u32 LE   flags (0xFF in CpkMaker output)
 *   +0x08  u64 LE   size of the @UTF table that follows
 *   +0x10  @UTF table, plain or XOR-obfuscated (byte ^= key, key starts at
 *          0x655F and is multiplied by 0x4115 after each byte)
 *
 * An @UTF table is BIG endian:
 *
 *   +0x00 "@UTF"  +0x04 u32 table size - 8   +0x08 u16 encoding
 *   +0x0A u16 rows offset   +0x0C u32 strings offset   +0x10 u32 data offset
 *   +0x14 u32 table name (string offset)   +0x18 u16 columns
 *   +0x1A u16 row width   +0x1C u32 rows      (offsets are relative to +8)
 *   +0x20 columns: u8 flags (storage 0x10 zero / 0x30 constant / 0x50 per
 *         row | type 0..11), u32 name; a constant column's value follows.
 *
 * Types: u8 s8 u16 s16 u32 s32 u64 s64 f32 f64, string (u32 offset in the
 * string pool), data (u32 offset in the data pool, u32 size).
 *
 * The "CPK " table's single row gives ContentOffset, TocOffset, ItocOffset
 * and Align.  With a TOC, each TOC row is one member: DirName, FileName,
 * FileSize, ExtractSize and FileOffset, which is relative to
 * min(ContentOffset, TocOffset).  Without a TOC the ITOC's DataL / DataH
 * sub-tables give ID, FileSize and ExtractSize; members are stored from
 * ContentOffset in ID order, each padded to Align, and are named by their
 * five-digit ID.
 *
 * A member that starts with "CRILAYLA" is compressed:
 *
 *   +0x00 "CRILAYLA"  +0x08 u32 LE unpacked size  +0x0C u32 LE packed size
 *   +0x10 packed bits, read backwards from the last packed byte, MSB first
 *   then  the uncompressed prefix (normally 0x100 bytes) that begins the
 *         output; the decoded bytes follow it, produced back to front.
 *
 * The bit stream: 0 + 8 bits = literal; 1 + 13 bits (distance - 3) + a
 * length whose extra count is read in 2, 3, 5 and then repeated 8-bit
 * fields, each all-ones field continuing the length (base length 3).
 */
typedef struct xx_cpk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t toc_offset;     /**< Absolute "TOC " packet offset, or -1. */
    int64_t itoc_offset;    /**< Absolute "ITOC" packet offset, or -1. */
    int64_t content_offset; /**< ContentOffset from the header, or -1. */
    bool encrypted;         /**< The header table was XOR obfuscated. */
} xx_cpk;

typedef xx_cpk xx_cpk_t;

XXFC_API void xx_cpk_init(xx_cpk *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_cpk *xx_cpk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cpk_destroy(xx_cpk *archive);
XXFC_API void xx_cpk_free(xx_cpk *archive);

XXFC_API bool xx_cpk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cpk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cpk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_cpk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cpk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cpk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one CRILAYLA stream held in memory.
 *
 * @param input       the member, starting at "CRILAYLA"
 * @param input_size  member size
 * @param output      receives prefix + unpacked bytes
 * @param output_size must equal (input_size - 0x10 - packed) + unpacked
 * @return true when exactly @p output_size bytes were produced
 */
XXFC_API bool xx_cpk_crilayla_decode(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CPK_H */
