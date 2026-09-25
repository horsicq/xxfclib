/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_ardi_diskette_image.h
 *  @brief ARDI self-extracting diskette image (Daniel F Valot, ARDI 4.31,
 *         built by IMG2ARDI): a 16-bit MZ + NE stub with a raw-Deflate
 *         floppy image behind its NE segment data. */

#ifndef XXFCLIB_FORMAT_SFX_ARDI_DISKETTE_IMAGE_H
#define XXFCLIB_FORMAT_SFX_ARDI_DISKETTE_IMAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest description text kept from the prologue (printable ASCII). */
#define XX_SFX_ARDI_DISKETTE_IMAGE_LABEL_MAX 200

/**
 * @brief An ARDI self-extracting diskette image.
 *
 * The carrier is an ordinary MZ + NE program for DOS and OS/2 1.x.  Nothing
 * in it is executed or emulated; only the fields below are read.
 *
 * The file ends in a 51-byte trailer whose last 31 bytes are the literal
 *
 *     "ARDI-(C)1991-" <4 decimal digits> "-Daniel Valot" 0x00
 *
 * (the 20 bytes in front of the text are not needed).  That is the one
 * marker at a fixed position, so it is checked first.
 *
 * The 0x33-byte record sits where the NE segment data ends (the highest
 * segment end, relocation records included).  It is looked for there
 * first; when the NE tables do not lead to it, the first 16 MiB are
 * scanned for its 85 04 00 00 00 run at +0x2A (at most 64 candidates).
 * A candidate is the record only when the stream it declares ends exactly
 * where the trailer starts:
 *
 *   +0x00  u16 LE  bytes per sector (512)         \
 *   +0x02  u8      sectors per cluster             |
 *   +0x03  u16 LE  reserved sectors                |
 *   +0x05  u8      number of FATs                  |  a copy of the
 *   +0x06  u16 LE  root directory entries          |  image's DOS BPB
 *   +0x08  u16 LE  total sectors (non-zero)        |
 *   +0x0A  u8      media descriptor                |
 *   +0x0B  u16 LE  sectors per FAT                 |
 *   +0x0D  u16 LE  sectors per track               |
 *   +0x0F  u16 LE  heads                           |
 *   +0x11  u32 LE  hidden sectors                  |
 *   +0x15  u32 LE  total sectors (32-bit)         /
 *   +0x19  u8[11]  drive geometry
 *   +0x24  u16 LE  0x55AA
 *   +0x26  u32 LE  compressed size (non-zero)
 *   +0x2A  u32 LE  0x00000485
 *   +0x2E  u8      0
 *   +0x2F  u32 LE  CRC-32 (zlib) of the diskette image
 *
 * One raw Deflate stream (no zlib or gzip wrapper) of exactly the declared
 * size follows at +0x33.  It inflates to a prologue of
 * [u8 tag][u32 LE length][length bytes] records ended by a 0xFF tag, and
 * then to the image itself, total sectors * 512 bytes.  Tag 0x80 is the
 * stub's message table; tag 0x02 is the builder's description,
 * [u32 count][count NUL-terminated strings], whose first string (the
 * label) is kept as the member comment.  The prologue may be at most
 * 64 KiB and 64 records.
 *
 * The container stores no name for the image (the stub writes it to a
 * diskette, or to a file the user names), so the one member is called
 * "disk.img".  Extraction succeeds only when the stream yields exactly the
 * image size after the prologue and the image matches the CRC-32.
 */
typedef struct xx_sfx_ardi_diskette_image {
    Abstractformat format;
    int64_t record_offset;    /**< Device offset of the 0x33-byte record. */
    int64_t stream_offset;    /**< Device offset of the Deflate stream. */
    int64_t stream_size;      /**< Compressed size from the record. */
    int64_t trailer_offset;   /**< Device offset of the 51-byte trailer. */
    int64_t prologue_size;    /**< Inflated bytes before the image; -1 if
                                   the prologue could not be measured. */
    uint64_t image_size;      /**< total_sectors * bytes_per_sector. */
    uint64_t number_of_records;
    uint32_t image_crc32;     /**< CRC-32 of the image, from the record. */
    uint16_t bytes_per_sector;
    uint16_t total_sectors;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint8_t media_descriptor;
    bool located_by_scan;     /**< Record found by the needle scan, not at
                                   the NE segment end. */
    char year[5];             /**< The four digits in the trailer. */
    char label[XX_SFX_ARDI_DISKETTE_IMAGE_LABEL_MAX + 1];
} xx_sfx_ardi_diskette_image;

typedef xx_sfx_ardi_diskette_image xx_sfx_ardi_diskette_image_t;

XXFC_API void xx_sfx_ardi_diskette_image_init(
    xx_sfx_ardi_diskette_image *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_ardi_diskette_image *xx_sfx_ardi_diskette_image_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_ardi_diskette_image_destroy(
    xx_sfx_ardi_diskette_image *archive);
XXFC_API void xx_sfx_ardi_diskette_image_free(
    xx_sfx_ardi_diskette_image *archive);

XXFC_API bool xx_sfx_ardi_diskette_image_check_is_valid(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_sfx_ardi_diskette_image_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_ardi_diskette_image_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_ardi_diskette_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_ardi_diskette_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_ardi_diskette_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_ardi_diskette_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_ardi_diskette_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_ardi_diskette_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Inflate the image into @p destination (NULL only verifies).
 *
 * Succeeds only when the prologue is well formed, exactly image_size bytes
 * follow it and their CRC-32 matches the record.
 */
XXFC_API bool xx_sfx_ardi_diskette_image_unpack_to_device(
    xx_sfx_ardi_diskette_image *archive, xx_io_device *destination,
    xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_ARDI_DISKETTE_IMAGE_H */
