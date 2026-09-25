/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_stuffit5.h @brief StuffIt 5 archive reader. */

#ifndef XXFCLIB_FORMAT_STUFFIT5_H
#define XXFCLIB_FORMAT_STUFFIT5_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A StuffIt 5 archive, as written by StuffIt 5.x-8.x on the Mac and
 * StuffIt 7 on Windows.  It is not the classic "SIT!" container (see
 * xx_stuffit.h) and not StuffIt X (.sitx).
 *
 * All integers are big-endian.  Archive header:
 *
 *   0x00  char[80] "StuffIt (c)1997-YYYY Aladdin Systems, Inc., ..." CR LF
 *   0x50  1A 00
 *   0x52  u8   format version, 5
 *   0x53  u8   archive flags (0x10 reserved block, 0x20 comment,
 *              0x80 password; their fields follow the header)
 *   0x54  u32  archive size
 *   0x58  u32  (an entry offset; not needed to walk the archive)
 *   0x5C  u16  number of root entries
 *   0x5E  u32  offset of the first root entry
 *   0x62  u16  CRC-16/ARC of bytes [0, first entry) with this field zeroed
 *
 * Entry header, "hs" bytes, then a second block:
 *
 *   0x00  u32  A5 A5 A5 A5
 *   0x04  u8   entry version (1 or 3)
 *   0x06  u16  hs, header size (the CRC below covers exactly hs bytes)
 *   0x09  u8   flags: 0x40 folder, 0x20 encrypted
 *   0x0A  u32  creation time, 0x0E u32 modification time (Mac epoch)
 *   0x12  u32  previous, 0x16 u32 next, 0x1A u32 parent entry offset
 *   0x1E  u16  name length
 *   0x20  u16  CRC-16/ARC of the hs header bytes with this field zeroed
 *   folder:  0x22 u32 first child offset (0xFFFFFFFF: end-of-folder
 *            marker, hs bytes long, no second block, not counted),
 *            0x26 u32 folder size, 0x2E u16 number of children
 *   file:    0x22 u32 data fork unpacked size, 0x26 u32 packed size,
 *            0x2A u16 data fork CRC-16/ARC, 0x2E u8 method,
 *            0x2F u8 password length, then the password data
 *   then the name, then an optional comment up to hs.
 *
 *   Second block, at +hs: u16 flags2 (bit 0: resource fork present),
 *   u16, Mac type, Mac creator, u16 Finder flags, then 22 bytes (entry
 *   version 1) or 18 bytes (other versions); when flags2 bit 0 is set:
 *   u32 resource unpacked size, u32 packed size, u16 CRC-16/ARC, u16,
 *   u8 method, u8 password length, the password data.
 *
 * The resource fork's packed bytes follow, then the data fork's.  Entries
 * are stored depth first; a folder's children start right after its second
 * block.
 *
 * Methods: 0 stored, 1 RLE90, 13 LZ+Huffman (checked against the CRC-16s)
 * and 15 Arsenic (arithmetic-coded BWT, checked against the CRC-32 at the
 * end of its stream).  Encrypted forks and other methods fail closed.
 */
typedef struct xx_stuffit5 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_size;
    uint32_t root_entries;
    uint8_t archive_flags;
} xx_stuffit5;

typedef xx_stuffit5 xx_stuffit5_t;

XXFC_API void xx_stuffit5_init(xx_stuffit5 *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_stuffit5 *xx_stuffit5_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_stuffit5_destroy(xx_stuffit5 *archive);
XXFC_API void xx_stuffit5_free(xx_stuffit5 *archive);

XXFC_API bool xx_stuffit5_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_stuffit5_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_stuffit5_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_stuffit5_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stuffit5_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stuffit5_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stuffit5_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stuffit5_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stuffit5_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STUFFIT5_H */
