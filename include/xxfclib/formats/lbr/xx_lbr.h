/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lbr.h @brief CP/M LU library (.LBR) reader. */

#ifndef XXFCLIB_FORMAT_LBR_H
#define XXFCLIB_FORMAT_LBR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CP/M "LU" library (Gary Novosielski's LU, NULU, LUU and kin).
 *
 * The file is a sequence of 128-byte CP/M sectors.  It opens with a
 * directory of 32-byte entries whose first entry describes the directory
 * itself:
 *
 *   +0   u8      status: 0x00 active, 0xFE deleted, 0xFF unused
 *   +1   char[8] name, space padded (bit 7 of each byte is a CP/M attribute)
 *   +9   char[3] extension, space padded (bit 7 likewise)
 *   +12  u16 LE  first sector of the member
 *   +14  u16 LE  length in sectors
 *   +16  u16 LE  CRC-16/XMODEM of the member's sectors (0 when not kept)
 *   +18  u16 LE  creation date, days since 1977-12-31 (0 = not set)
 *   +20  u16 LE  last change date, same epoch
 *   +22  u16 LE  creation time, DOS layout
 *   +24  u16 LE  last change time, DOS layout
 *   +26  u8      pad count: unused bytes in the member's last sector
 *   +27  u8[5]   reserved
 *
 * The directory entry has status 0, a blank name and first sector 0; its
 * length is the directory size in sectors.  Members are stored raw (often
 * already squeezed or crunched, which other readers handle); this reader
 * lists them in directory order and copies each one out byte for byte.
 */
typedef struct xx_lbr {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t directory_sectors;
    /** Active directory entries, including those whose data lies past EOF. */
    uint32_t active_entries;
    /** Active entries left out because their data runs past EOF. */
    uint32_t truncated_entries;
} xx_lbr;

typedef xx_lbr xx_lbr_t;

/** Size of one CP/M record ("sector") and of one directory entry. */
#define XX_LBR_SECTOR_SIZE 128
#define XX_LBR_ENTRY_SIZE 32

XXFC_API void xx_lbr_init(xx_lbr *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_lbr *xx_lbr_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_lbr_destroy(xx_lbr *archive);
XXFC_API void xx_lbr_free(xx_lbr *archive);

XXFC_API bool xx_lbr_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lbr_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_lbr_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_lbr_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lbr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lbr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lbr_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lbr_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lbr_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LBR_H */
