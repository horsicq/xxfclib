/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_isz.h @brief UltraISO ISZ ("ISO zipped") compressed disc image. */

#ifndef XXFCLIB_FORMAT_ISZ_H
#define XXFCLIB_FORMAT_ISZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ISZ image: a disc image cut into fixed-size chunks, each chunk
 * stored, zlib, bzip2 or all-zero, presented as the single image it rebuilds.
 *
 * Header, little endian, packed (48 bytes, 64 with the checksum tail):
 *
 *   0x00  char[4] "IsZ!"          0x04  u8  header size (48 / 64)
 *   0x05  u8  version (<= 1)      0x06  u32 volume serial number
 *   0x0A  u16 sector size         0x0C  u32 total sectors
 *   0x10  u8  encryption: 0 none, 1 legacy password, 2/3/4 AES-128/192/256
 *   0x11  u64 segment (volume) size, 0 = single file
 *   0x19  u32 chunk count         0x1D  u32 chunk size (sector multiple)
 *   0x21  u8  chunk pointer size  0x22  u8  volume number of this file
 *   0x23  u32 chunk pointer table offset (0 = none: every chunk stored)
 *   0x27  u32 segment table offset (0 = none)
 *   0x2B  u32 chunk data offset   0x2F  u8  reserved
 *   0x30  u32 checksum1, u32 data size, u32 unknown, u32 checksum2
 *
 * Chunk pointers are `pointer size` bytes little endian: the top two bits
 * are the chunk type (0 zeros, 1 stored, 2 zlib, 3 bzip2), the rest the
 * stored length.  Chunk data follows back to back from the data offset.
 * The pointer table and the 24-byte segment records (u64 volume size,
 * u32 chunk count, u32 first chunk, u32 chunk data offset, u32 bytes of
 * the last chunk that continue in the next volume; a zero size ends the
 * table) are XORed with the bitwise NOT of "IsZ!".  AES modes encrypt the
 * whole 16-byte blocks of each chunk with ECB, keyed by the password
 * zero-padded to the key size.
 *
 * Only the first file of a volume set is accepted; a set that spans
 * several files is recognised and listed, but its image cannot be rebuilt
 * from the first file alone.
 */
typedef struct xx_isz {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t image_size;     /**< total sectors * sector size */
    uint32_t chunk_count;
    uint32_t chunk_size;
    uint8_t encryption;      /**< header encryption byte (0 = none) */
    bool multi_volume;       /**< image continues in .i01, .i02, ... */
} xx_isz;

typedef xx_isz xx_isz_t;

XXFC_API void xx_isz_init(xx_isz *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_isz *xx_isz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_isz_destroy(xx_isz *archive);
XXFC_API void xx_isz_free(xx_isz *archive);

XXFC_API bool xx_isz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_isz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_isz_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_isz_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_isz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_isz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_isz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_isz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_isz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ISZ_H */
