/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gbi.h @brief gBurner GBI compressed disc image reader. */

#ifndef XXFCLIB_FORMAT_GBI_H
#define XXFCLIB_FORMAT_GBI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief gBurner GBI image: the PowerISO DAA container under another
 * signature.  It holds one ISO-9660 data track cut into fixed-size chunks,
 * each compressed on its own.  All fields are little endian.
 *
 *   0x00  char[16] "GBI" padded with zero bytes ("GBI VOL" = later volume)
 *   0x10  u32  chunk table offset
 *   0x14  u32  format version: 0x100 or 0x110
 *   0x18  u32  chunk data offset (version 0x110: low 24 bits only)
 *   0x1C  u32  1
 *   0x20  u32  0
 *   0x24  u32  chunk size (0x100); for 0x110 a bit field:
 *              bits 0-11  chunk size in 16 KiB units
 *              bit 14     chunk table is compressed (not supported)
 *              bit 17     chunk table bit fields are xor-scrambled
 *              bit 20     LZMA in use
 *              bits 23-24 deflate block-type permutation (xor 1 for GBI)
 *              bit 27     chunk table is additionally DAA-scrambled
 *   0x28  u64  image (ISO) size
 *   0x30  u64  container size
 *   0x38  u8   profile            (0x110 only, otherwise zero)
 *   0x39  u32  compressed table size
 *   0x3D  u8   table bit sizes: bits 0-2 type width, bits 3-7 length width
 *              (0 = derived from the chunk size, else value + 10)
 *   0x3E  u8   LZMA filter: 0 none, 1 x86 BCJ
 *   0x3F  u8[5] LZMA properties
 *   0x44  u8[4] reserved
 *   0x48  u32  CRC-32 of bytes 0x00..0x47
 *   0x4C  descriptors {u32 type, u32 length incl. this header, data} up to
 *         the chunk table: 1 part, 2 split (u32 part count), 3 encryption,
 *         4 comment
 *
 * The chunk table runs from its offset to the chunk data offset.  GBI
 * scrambles every byte as ((plain ^ (u8)(table_len / 4)) + (u8)crc).
 * Version 0x100 entries are 3 bytes (length = b0<<16 | b2<<8 | b1, deflate).
 * Version 0x110 entries are LSB-first bit fields: length - 5, then the type
 * (0 LZMA, 1 deflate); a length of at least the chunk size means the chunk
 * is stored (payload plus 4 trailing bytes).  Chunks are raw deflate (with
 * the header's block-type permutation) or raw LZMA with the header's
 * properties and no end marker, each decoding to one full chunk except the
 * last.
 *
 * The reader exposes one record, "image.iso".  Encrypted images, split
 * (multi-volume) sets and compressed chunk tables are recognised and listed
 * but not unpacked.
 */
typedef struct xx_gbi {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t image_size;
    uint32_t version;
    uint32_t chunk_size;
    uint64_t chunk_count;
    bool encrypted;
    bool multi_volume;
    bool unsupported; /**< compressed table or other layout we cannot read */
} xx_gbi;

typedef xx_gbi xx_gbi_t;

XXFC_API void xx_gbi_init(xx_gbi *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_gbi *xx_gbi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_gbi_destroy(xx_gbi *archive);
XXFC_API void xx_gbi_free(xx_gbi *archive);

XXFC_API bool xx_gbi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gbi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gbi_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_gbi_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gbi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gbi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gbi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gbi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gbi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GBI_H */
