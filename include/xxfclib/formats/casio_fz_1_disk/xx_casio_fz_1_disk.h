/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_casio_fz_1_disk.h @brief Casio FZ-1 sampler floppy image reader. */

#ifndef XXFCLIB_FORMAT_CASIO_FZ_1_DISK_H
#define XXFCLIB_FORMAT_CASIO_FZ_1_DISK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Casio FZ-1 / FZ-10M / FZ-20M (and Hohner HS-1) floppy image.
 *
 * The raw sector dump of a 3.5" HD disk formatted by the sampler: 80
 * tracks, 2 sides, 8 sectors of 1024 bytes, stored in logical order
 * loc = 16 * track + 8 * side + (sector - 1).  An image is exactly
 * 1280 * 1024 = 1,310,720 bytes.  All integers are little-endian.
 *
 *   sector 0  head: disk ID and cluster allocation table
 *     +0x000  char[12] disk name
 *     +0x00C  u8[4]    00 00 02 00
 *     +0x010  char[12] password
 *     +0x01C  u8[4]    00 00 00 00
 *     +0x080  u8[768]  CAT, one bit per sector (LSB first); sectors 0 and 1
 *                      are always marked used
 *   sector 1  directory: 64 entries of 16 bytes
 *     +0x0    char[12] file name, space padded; a NUL first byte = blank
 *     +0xC    u8       content type: 0 full dump, 1 voice, 2 bank,
 *                      3 effect, 4 sequence, 5 expanded program
 *     +0xD    u8       0 = first disk of a set, 1 = continuation disk
 *     +0xE    u16      head sector of the file
 *   file head sector
 *     +0x000  64 x {u16 start, u16 end} data block pointers, an inclusive
 *             sector range each; {0, 0} ends the list.  The first range
 *             normally starts at the head sector itself.
 *     +0x3FA  u16 bank count, u16 voice count, u16 wave block count
 *
 * Each directory entry is published as one member: the file's sectors in
 * data-block-pointer order without the head sector, which is the layout of
 * the .FZF / .FZV / .FZB files that other FZ tools exchange.  Members are
 * named "<name>.<ext>" with ext fzf, fzv, fzb, fze, fzs or fzp by content
 * type, and ".disk2" in front of the extension for a continuation part.
 */
typedef struct xx_casio_fz_1_disk {
    Abstractformat format;
    uint64_t number_of_records;
    char label[13]; /**< Disk name, trailing spaces removed. */
} xx_casio_fz_1_disk;

typedef xx_casio_fz_1_disk xx_casio_fz_1_disk_t;

#define XX_CASIO_FZ_1_DISK_SECTOR_SIZE 1024
#define XX_CASIO_FZ_1_DISK_SECTOR_COUNT 1280
#define XX_CASIO_FZ_1_DISK_IMAGE_SIZE \
    (XX_CASIO_FZ_1_DISK_SECTOR_SIZE * XX_CASIO_FZ_1_DISK_SECTOR_COUNT)

XXFC_API void xx_casio_fz_1_disk_init(xx_casio_fz_1_disk *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_casio_fz_1_disk *xx_casio_fz_1_disk_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_casio_fz_1_disk_destroy(xx_casio_fz_1_disk *archive);
XXFC_API void xx_casio_fz_1_disk_free(xx_casio_fz_1_disk *archive);

XXFC_API bool xx_casio_fz_1_disk_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_casio_fz_1_disk_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_casio_fz_1_disk_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_casio_fz_1_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_casio_fz_1_disk_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_casio_fz_1_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_casio_fz_1_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_casio_fz_1_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_casio_fz_1_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CASIO_FZ_1_DISK_H */
