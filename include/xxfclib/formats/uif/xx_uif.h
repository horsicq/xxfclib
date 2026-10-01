/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uif.h @brief MagicISO UIF ("Universal Image Format") disc image. */

#ifndef XXFCLIB_FORMAT_UIF_H
#define XXFCLIB_FORMAT_UIF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MagicISO UIF image: compressed disc blocks plus trailing tables.
 *
 * Every integer is little-endian.  The 64-byte "bbis" trailer is the last
 * thing in the file:
 *
 *   0x00  char[4] "bbis"
 *   0x04  u32     trailer size (0x40)
 *   0x08  u16     version: <= 2 blocks are zlib, >= 3 LZMA
 *   0x0A  u16     image type: 8 ISO, 9 raw / mixed (BIN, MDF, IMG, NRG)
 *   0x0C  u16     unknown, 0x0E u16 padding
 *   0x10  u32     image size in sectors
 *   0x14  u32     sector size
 *   0x18  u32     bytes used in the last sector (0 = all of it)
 *   0x1C  u64     offset of the "blhr" block table
 *   0x24  u32     blhr size
 *   0x28  u8[16]  hash
 *   0x38  u8      fixed-key index (1..16 = DES with a built-in key)
 *   0x39  u8      2 = MagicISO's private cipher
 *   0x3A  u8[2], 0x3C u32  cipher parameters
 *
 * "blhr" (or "bsdr" when password protected): u32 size (counting from
 * offset 8), u32 compressed flag, u32 entry count, then the table, packed
 * (size - 8 bytes) or plain (count * 24 bytes).  Entry: u64 offset, u32
 * packed size, u32 first sector, u32 sector count, u32 type (1 stored and
 * zero padded, 3 all zero, 5 packed).  A zlib block is an RFC 1950 stream;
 * an LZMA block is u8 x86-filter flag, 5 property bytes, u64 size, raw LZMA.
 *
 * Image type 9 follows the table with "blms" (the track layout) and
 * "blss" (u32 output format 0 ISO / 1 BIN+CUE / 2 MDF+MDS / 3 IMG+CCD+SUB /
 * 4 NRG, then the original descriptor file(s)).
 *
 * The image is presented as image.<ext>; for image type 9 the descriptor
 * files stored in blss follow as image.cue / image.mds / image.ccd +
 * image.sub.  Encrypted images are listed but not extracted.
 */
typedef struct xx_uif {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t image_size;     /**< bytes the image member unpacks to */
    uint32_t sectors;
    uint32_t sector_size;
    uint32_t block_count;
    uint16_t version;
    uint16_t image_type;
    uint32_t output_format;  /**< blss output format (0 for image type 8) */
    bool encrypted;
} xx_uif;

typedef xx_uif xx_uif_t;

XXFC_API void xx_uif_init(xx_uif *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_uif *xx_uif_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_uif_destroy(xx_uif *archive);
XXFC_API void xx_uif_free(xx_uif *archive);

XXFC_API bool xx_uif_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uif_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_uif_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_uif_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_uif_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_uif_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_uif_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_uif_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_uif_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UIF_H */
