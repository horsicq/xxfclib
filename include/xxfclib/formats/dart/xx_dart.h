/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dart.h @brief Apple DART (Disk Archival/Retrieval Tool) image reader. */

#ifndef XXFCLIB_FORMAT_DART_H
#define XXFCLIB_FORMAT_DART_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DART disk image: a compressed copy of a Mac, Lisa, Apple II or
 * MS-DOS floppy. There is no magic; everything is big endian.
 *
 *   0x00  u8      compression: 0 fast (word RLE), 1 best (LZHUF), 2 none
 *   0x01  u8      disk type: 1 Mac, 2 Lisa, 3 Apple II (400K or 800K);
 *                 0x10 Mac HD, 0x11 MS-DOS 720K, 0x12 MS-DOS 1440K
 *                 (720K or 1440K)
 *   0x02  u16     disk size in KiB
 *   0x04  u16[n]  stored length of each block; n = 72 when the size is above
 *                 800K, else 40. The first size/20 entries are non-zero, the
 *                 rest zero. 0xFFFF means the block is stored raw. Otherwise
 *                 RLE lengths count 16-bit words and LZHUF lengths bytes;
 *                 "none" images store 20960 or 0xFFFF.
 *   then the blocks back to back. Each expands to 20960 bytes: 20480 bytes of
 *   sector data (40 sectors of 512) followed by 480 bytes of tags (12 per
 *   sector).
 *
 * RLE: a signed u16 count; a positive count is followed by that many literal
 * words, a negative one by one word repeated -count times; zero is invalid.
 * LZHUF: Okumura's LZSS + adaptive Huffman (4 KiB ring, 60-byte look-ahead,
 * threshold 2), ring preset to zero, every block coded independently.
 *
 * The reader exposes two members: "image.img" (the sector data, size * 1024
 * bytes) and "tags.bin" (the tag bytes, size * 24 bytes).
 */
typedef struct xx_dart {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t compression; /**< 0 RLE, 1 LZHUF, 2 none. */
    uint32_t disk_type;
    uint32_t disk_kib;
    uint32_t block_count;   /**< disk_kib / 20 */
    uint32_t header_size;   /**< 84 or 148 */
    int64_t stored_size;    /**< Header plus every stored block. */
} xx_dart;

typedef xx_dart xx_dart_t;

#define XX_DART_BLOCK_DATA 20480
#define XX_DART_BLOCK_TAGS 480
#define XX_DART_BLOCK_SIZE (XX_DART_BLOCK_DATA + XX_DART_BLOCK_TAGS)

XXFC_API void xx_dart_init(xx_dart *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_dart *xx_dart_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dart_destroy(xx_dart *archive);
XXFC_API void xx_dart_free(xx_dart *archive);

XXFC_API bool xx_dart_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dart_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dart_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_dart_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dart_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dart_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dart_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dart_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dart_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Expand the whole image.
 *
 * @param data_destination receives the sector data (NULL discards it)
 * @param tag_destination  receives the tag bytes (NULL discards them)
 * @return true only when every block expanded to exactly 20960 bytes
 */
XXFC_API bool xx_dart_unpack_to_device(xx_dart *archive,
                                       xx_io_device *data_destination,
                                       xx_io_device *tag_destination,
                                       xx_pd_struct *pd);

/**
 * @brief Expand one LZHUF-coded DART block held in memory.
 *
 * Bytes past @p input_size read as zero, as in the reference decoder.
 * @return true when @p output_size bytes were produced
 */
XXFC_API bool xx_dart_lzhuf_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DART_H */
