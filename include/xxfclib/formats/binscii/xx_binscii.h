/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_binscii.h @brief BinSCII (Apple II text transport) reader. */

#ifndef XXFCLIB_FORMAT_BINSCII_H
#define XXFCLIB_FORMAT_BINSCII_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BinSCII text file: one or more segments, each carrying a slice of
 * one ProDOS file.
 *
 * Every segment is a run of text lines (CR, LF or CR LF; leading bytes
 * <= 0x20 on a line are ignored, and text between segments is skipped):
 *
 *   FiLeStArTfIlEsTaRt
 *   <alphabet>       64 distinct characters; the i-th one encodes the value i
 *   <header>         char 0: '@' + name length (1..15); chars 1..15: the
 *                    ProDOS name, space padded; chars 16..51: 9 units
 *                    decoding to 27 bytes:
 *                      +0  u24 LE file length     +3  u24 LE segment offset
 *                      +6  u8 access  +7 u8 file type  +8 u16 LE aux type
 *                      +10 u8 storage type        +11 u16 LE size in blocks
 *                      +13 u16 cdate  +15 u16 ctime  +17 u16 mdate
 *                      +19 u16 mtime  +21 u24 LE segment length
 *                      +24 u16 LE CRC-16/XMODEM of bytes 0..23   +26 pad
 *   <data lines>     ceil(segment length / 48) lines of 64 characters, each
 *                    16 units = 48 bytes (the last one zero padded)
 *   <CRC line>       one unit: u16 LE CRC-16/XMODEM over every decoded data
 *                    byte of the segment, padding included
 *
 * A unit is 4 characters c0..c3 with values v0..v3 that decode to
 *   (v3 << 2 | v2 >> 4), (v2 << 4 | v1 >> 2), (v1 << 6 | v0).
 *
 * A segment with offset 0 starts a new member; later segments of the same
 * member follow with offset == bytes decoded so far.  Each member whose
 * first segment is in the input is one archive record.  Segments whose
 * member started in another input file (split postings) are skipped.
 */
typedef struct xx_binscii {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_segments;
} xx_binscii;

typedef xx_binscii xx_binscii_t;

/** The segment signature line. */
#define XX_BINSCII_SIGNATURE "FiLeStArTfIlEsTaRt"
#define XX_BINSCII_SIGNATURE_SIZE 18

XXFC_API void xx_binscii_init(xx_binscii *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_binscii *xx_binscii_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_binscii_destroy(xx_binscii *archive);
XXFC_API void xx_binscii_free(xx_binscii *archive);

XXFC_API bool xx_binscii_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_binscii_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_binscii_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_binscii_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_binscii_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_binscii_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_binscii_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_binscii_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_binscii_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BINSCII_H */
