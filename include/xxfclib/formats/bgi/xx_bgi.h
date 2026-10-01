/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bgi.h @brief BGI / Ethornell version 1 "PackFile" archive reader. */

#ifndef XXFCLIB_FORMAT_BGI_H
#define XXFCLIB_FORMAT_BGI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BGI (Buriko General Interpreter / Ethornell) resource archive,
 * version 1 ("PackFile    ", usually *.arc).
 *
 * Every field is LITTLE endian:
 *   0x00  char[12] "PackFile    " (four trailing spaces)
 *   0x0C  u32 count: number of entries, 1..0xFFFFF
 *   0x10  count entries of 0x20 bytes each:
 *           +0x00 char[0x10] name, Shift-JIS, NUL-padded (may fill all 0x10)
 *           +0x10 u32 offset, relative to the data area
 *           +0x14 u32 size
 *           +0x18 8 bytes the engine does not need (zero or unknown)
 *   0x10 + 0x20 * count: the data area
 *
 * Members are stored as the engine reads them ("DSC FORMAT 1.00" compressed
 * scripts, "CompressedBG___" pictures, "bw  " audio, ...); the reader
 * extracts them byte for byte and does not decode those member formats.
 *
 * The version 2 archive ("BURIKO ARC20", 0x80-byte entries) is the bgi2
 * reader.
 *
 * Validation: the magic, a count of 1..0xFFFFF, the whole index inside the
 * file, and every member [data + offset, data + offset + size) inside the
 * file.  The format size is the end of the furthest member (at least the end
 * of the index).
 *
 * Member names are made safe and unique before anything is written:
 *   - '\\' and '/' become '/';
 *   - a Shift-JIS double-byte character becomes "%XX%XX", any other byte
 *     >= 0x80 or < 0x20 becomes "%XX" (upper-case hex), '%' becomes "%25";
 *   - an empty name becomes "%_<entry index>";
 *   - a name that repeats an earlier one (compared case-insensitively) gets
 *     "%_<entry index>" inserted before its extension;
 *   - a name with an empty, ".", ".." or trailing-dot/space component, a
 *     Windows device name, or one of : < > " | ? * is listed but refused on
 *     extraction.
 */
typedef struct xx_bgi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t data_base; /**< Offset of the data area from the archive start. */
} xx_bgi;

typedef xx_bgi xx_bgi_t;

XXFC_API void xx_bgi_init(xx_bgi *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_bgi *xx_bgi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bgi_destroy(xx_bgi *archive);
XXFC_API void xx_bgi_free(xx_bgi *archive);

XXFC_API bool xx_bgi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bgi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bgi_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_bgi_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bgi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bgi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bgi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bgi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bgi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BGI_H */
