/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bondwell_2_disk.h @brief Bondwell 2 CP/M disk image reader. */

#ifndef XXFCLIB_FORMAT_BONDWELL_2_DISK_H
#define XXFCLIB_FORMAT_BONDWELL_2_DISK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Bondwell 2 floppy image (.dsk) holding a CP/M 2.2 filesystem.
 *
 * Image: a headerless dump of a single-sided 3.5" double-density disk,
 * 80 tracks of 256-byte sectors, track after track, each track's sectors in
 * sector-ID order (IDs start at 0).  MAME's bw2 format knows two sizes:
 *   17 sectors per track  348160 bytes ("340K")
 *   18 sectors per track  368640 bytes ("360K", the machine's own format)
 *
 * CP/M layout (Bondwell 2 BIOS, 22DISK "BON3"): 2 reserved system tracks,
 * no software skew, 2048-byte allocation blocks numbered from the first
 * sector of track 2, so block B lives at image offset
 * 2 * spt * 256 + B * 2048.  That gives 175 blocks (DSM 174) on the 18-sector
 * disk and 165 (DSM 164) on the 17-sector one; both are under 256, so a
 * directory entry holds sixteen 8-bit block numbers and EXM is 1 (one entry
 * covers two 16K logical extents).
 *
 * The directory starts at block 0.  Published definitions disagree on its
 * size (128 entries = blocks 0-1, or 64 entries = block 0), so it is decided
 * per image: 64 entries when a live entry of block 0 allocates block 1,
 * otherwise 128 when all of block 1 also reads as valid directory entries,
 * otherwise 64.
 *
 * Directory entry, 32 bytes:
 *   0   user number 0..15; 0xE5 = free / deleted
 *   1   char[8] name, 7-bit ASCII padded with spaces; bit 7 of bytes 1..4
 *       are the F1'..F4' attributes
 *   9   char[3] type; bit 7 of byte 9 = read-only, 10 = system, 11 = archive
 *   12  EX  extent number, low 5 bits (0..31)
 *   13  S1  reserved
 *   14  S2  extent number, high bits (0..15 for CP/M 2.2's 8 MB limit)
 *   15  RC  records (128 bytes) in the entry's last logical extent, 0..128
 *   16  u8[16] block numbers, 0 = not allocated
 * The logical extent L = S2 * 32 + EX; the entry holds file records
 * (L >> 1) * 256 onwards, (L & 1) * 128 + RC of them, and the file's length
 * in records is the maximum of L * 128 + RC over its entries.  CP/M 2.2
 * files are whole records: a member is records * 128 bytes, with the
 * application's padding (usually ^Z) left in place.  An unallocated block
 * number inside a file, or a missing entry, is a sparse hole and reads as
 * zeros.
 *
 * Detection: there is no magic (track 0 is boot code), so this is a late
 * probe.  The image must be exactly one of the two sizes, every directory
 * entry must be free or valid (user 0..15, a non-empty name of printable
 * ASCII without the CP/M delimiters . , ; : = ? * < > [ ] and with padding
 * only at the end, EX <= 31, S2 <= 15, RC <= 128, S1 <= 128, block numbers
 * inside the data area and outside the directory, no block number past the
 * entry's own record count, no block allocated twice, no duplicate extent),
 * and at least one file must exist.  A file whose length would exceed the
 * disk's data area is refused as well.  Anything else rejects the whole
 * image; a freshly formatted disk with no files is not claimed.
 *
 * Members are the files, in the directory order of their first entry.  The
 * name is "NAME.TYP" ("NAME" when the type is blank); files of user areas
 * 1..15 go in a folder "userN/".  Characters outside printable ASCII and the
 * characters / \ : * ? " < > | ~ become '_'; a Windows device stem (CON,
 * PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$) gets a '_' in
 * front.  A name that repeats an earlier one, compared case-insensitively,
 * gets "~<directory index of its first entry>" before the type, so every
 * member name is unique.  XX_META_ID_ATTRIBUTES carries the CP/M attribute
 * bits: 0x01 read-only, 0x02 system, 0x04 archive, 0x10..0x80 F1'..F4'.
 * The system tracks are not exposed.
 */
typedef struct xx_bondwell_2_disk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;          /**< 348160 or 368640. */
    uint32_t sectors_per_track;  /**< 17 or 18. */
    uint32_t block_count;        /**< DSM + 1: 165 or 175. */
    uint32_t directory_entries;  /**< 64 or 128. */
} xx_bondwell_2_disk;

typedef xx_bondwell_2_disk xx_bondwell_2_disk_t;

XXFC_API void xx_bondwell_2_disk_init(xx_bondwell_2_disk *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_bondwell_2_disk *xx_bondwell_2_disk_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_bondwell_2_disk_destroy(xx_bondwell_2_disk *archive);
XXFC_API void xx_bondwell_2_disk_free(xx_bondwell_2_disk *archive);

XXFC_API bool xx_bondwell_2_disk_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_bondwell_2_disk_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_bondwell_2_disk_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_bondwell_2_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_bondwell_2_disk_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bondwell_2_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bondwell_2_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bondwell_2_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bondwell_2_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BONDWELL_2_DISK_H */
