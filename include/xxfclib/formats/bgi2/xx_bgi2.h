/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bgi2.h @brief BGI / Ethornell "BURIKO ARC20" archive reader. */

#ifndef XXFCLIB_FORMAT_BGI2_H
#define XXFCLIB_FORMAT_BGI2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BGI (Buriko General Interpreter / Ethornell) resource archive,
 * version 2 ("BURIKO ARC20", usually *.arc).
 *
 * Every field is LITTLE endian:
 *   0x00  char[12] "BURIKO ARC20"
 *   0x0C  u32 count: number of entries, 1..0x3FFFF
 *   0x10  count entries of 0x80 bytes each:
 *           +0x00 char[0x60] name, Shift-JIS, NUL-padded (may fill all 0x60)
 *           +0x60 u32 offset, relative to the data area
 *           +0x64 u32 size
 *           +0x68 24 bytes the engine does not need (zero or unknown)
 *   0x10 + 0x80 * count: the data area
 *
 * Members are stored as the engine reads them.  Many are themselves BGI
 * resources ("DSC FORMAT 1.00" compressed scripts, "BSE 1.x" header-scrambled
 * images, "CompressedBG___" pictures, "bw  " audio); the reader extracts them
 * byte for byte and does not decode those member formats.
 *
 * The version 1 archive ("PackFile    ", 0x20-byte entries) is a different
 * reader.
 *
 * Validation: the magic, a count of 1..0x3FFFF, the whole index inside the
 * file, and every member [data + offset, data + offset + size) inside the
 * file.  The format size is the end of the furthest member (at least the end
 * of the index).
 *
 * Member names are made safe and unique before anything is written:
 *   - '\\' and '/' become '/';
 *   - a Shift-JIS double-byte character becomes "%XX%XX"; any other byte
 *     >= 0x80, < 0x20 or 0x7F, and each of % : < > " | ? * becomes "%XX"
 *     (upper-case hex);
 *   - an empty name becomes "%_<entry index>";
 *   - a name that repeats an earlier one (compared case-insensitively) gets
 *     "%_<entry index>" inserted before its extension;
 *   - a name with a leading '/', an empty, ".", ".." or trailing-dot/space
 *     component, or a Windows device name (CON, PRN, AUX, NUL, COM0-9,
 *     LPT0-9, CLOCK$, CONIN$, CONOUT$) is listed but refused on extraction.
 */
typedef struct xx_bgi2 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t data_base; /**< Offset of the data area from the archive start. */
} xx_bgi2;

typedef xx_bgi2 xx_bgi2_t;

XXFC_API void xx_bgi2_init(xx_bgi2 *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_bgi2 *xx_bgi2_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bgi2_destroy(xx_bgi2 *archive);
XXFC_API void xx_bgi2_free(xx_bgi2 *archive);

XXFC_API bool xx_bgi2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bgi2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bgi2_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_bgi2_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bgi2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bgi2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bgi2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bgi2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bgi2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BGI2_H */
