/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bga.h @brief BGA32 archive reader (.gza / .bza). */

#ifndef XXFCLIB_FORMAT_BGA_H
#define XXFCLIB_FORMAT_BGA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BGA archive, written by the Japanese BGA32.DLL archiver: .gza
 * (every member gzip-coded) or .bza (every member bzip2-coded).
 *
 * The archive is a plain sequence of members, each a 28-byte header, the
 * name and the data.  Every field is LITTLE endian:
 *   0x00  i32  checksum: the sum of the header bytes 0x04..0x1B and of the
 *              name bytes, each taken as a SIGNED char
 *   0x04  char[4] method: "GZIP" or "BZ2\0"
 *   0x08  u32  compressed size (data only)
 *   0x0C  u32  original size
 *   0x10  u16  DOS date        0x12  u16  DOS time
 *   0x14  u8   attributes (0x10 directory)
 *   0x15  u8   header type (always 0)
 *   0x16  u16  arc type: 1 compressed, 2 stored, 0 compressed unless the
 *              sizes are equal and the name ends in an archive extension
 *              (.arc .arj .bz2 .bza .cab .gz .gza .lzh .lzs .pak .rar .taz
 *              .tbz .tgz .z .zip .zoo), in which case stored
 *   0x18  u16  directory-name length   0x1A  u16  file-name length
 *   0x1C  directory name, then file name (Shift-JIS, no NUL); a name that
 *         ends in '\\' is a directory entry
 * A "GZIP" member's data is one gzip member (header, raw Deflate, CRC-32,
 * ISIZE); a "BZ2" member's data is one bzip2 stream.
 *
 * The walk starts at the base address and stops at the end of the device
 * or at the first header whose checksum does not match; anything after
 * that is overlay.  Only the first member must lie wholly inside the
 * device; a later member whose data is cut short is listed and fails to
 * extract.  At most 1,048,576 members are listed.
 *
 * Member names follow this library's Shift-JIS convention (sar_ns, nsa,
 * noa): '\\' becomes '/', a double-byte character becomes "%XX%XX", any
 * other byte >= 0x80 and '%' become "%XX"; a repeated path (compared
 * case-insensitively) gets "%_<index>" before its extension; unsafe paths
 * are listed but refused on extraction.
 */
typedef struct xx_bga {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bga;

typedef xx_bga xx_bga_t;

XXFC_API void xx_bga_init(xx_bga *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_bga *xx_bga_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bga_destroy(xx_bga *archive);
XXFC_API void xx_bga_free(xx_bga *archive);

XXFC_API bool xx_bga_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bga_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bga_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bga_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bga_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bga_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bga_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bga_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bga_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BGA_H */
