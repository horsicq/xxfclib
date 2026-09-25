/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rsdos_fs.h @brief Tandy Color Computer RS-DOS (Disk Extended
 * Color BASIC) filesystem reader. */

#ifndef XXFCLIB_FORMAT_RSDOS_FS_H
#define XXFCLIB_FORMAT_RSDOS_FS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RS-DOS volume on a flat CoCo floppy image.
 *
 * Containers read directly - all three store plain 256-byte sectors, 18 per
 * track, sector 1 first, track by track, and on a two-sided image head 0 and
 * head 1 of one track in turn:
 *   - headerless .dsk / JVC: size is a multiple of 256, one side;
 *   - JVC with a header of (size mod 256) bytes: sectors per track (must be
 *     18), sides (1 or 2), sector size code (must be 1 = 256), first sector
 *     ID (must be 1), sector attribute flag (must be 0); bytes past the fifth
 *     are ignored;
 *   - VDK: "dk", u16 LE header size (= size mod 256, >= 12), tracks at +8,
 *     sides at +9, compression bits (+11 & 7) must be 0.
 * Sector-level containers (DMK, IMD, TeleDisk, CopyQM, D88, CPC DSK, FDI)
 * are not decoded here; a reader that rebuilds the flat sector image can
 * hand it to this one through a device and base address.
 *
 * Filesystem: track 17, head 0 is the directory track.  Sector 2 is the
 * granule table (FAT): one byte per granule, 0xFF free, 0xC0 + n the last
 * granule of a file with n (0..9) sectors used, otherwise the next granule.
 * Sectors 3..11 hold 72 directory entries of 32 bytes:
 *   0   char[8] name, space padded; byte 0 = 0x00 deleted, 0xFF end
 *   8   char[3] extension, space padded
 *   11  u8  file type: 0 BASIC, 1 BASIC data, 2 machine code, 3 text
 *   12  u8  ASCII flag: 0x00 binary, 0xFF ASCII
 *   13  u8  first granule
 *   14  u16 BIG endian: bytes used in the last sector, 0..256
 * A granule is half a track, 9 sectors / 2304 bytes: granule g is track
 * g / 2 (plus one from track 17 on, skipping the directory track), head 0,
 * sectors 1..9 when g is even and 10..18 when odd.  Head 1 of a two-sided
 * image is not part of the volume.  The granule count is 2 * (tracks - 1)
 * capped at 255, with at least 35 tracks assumed (68 granules).
 *
 * File size = 2304 per granule before the last, plus for the last granule
 * (n - 1) * 256 + last-sector bytes, where a stored 0 counts as a full
 * 256-byte sector (as MAME imgtool reads it), and nothing when n = 0.
 *
 * Detection is a late probe (no magic): a size of at least 18 whole tracks
 * and at most 255, a valid container header, every granule-table byte one
 * of the three legal kinds, and every live entry before the end marker with
 * a printable, not blank name and extension, type 0..3, ASCII flag 0x00 or
 * 0xFF, last-sector bytes <= 256 and a granule chain that ends in 0xC0..0xC9
 * without running into a free, out-of-range or already visited granule.  At
 * least one live entry is required.  Any violation rejects the image, as
 * MAME imgtool rejects it with "Corrupt image".
 *
 * Members are the live entries in directory order, named "NAME.EXT" (or
 * "NAME" when the extension is blank) after trailing spaces are trimmed.
 * Leading spaces, trailing dots, '~' and the characters / \ : * ? " < > |
 * become '_'; a Windows device stem (CON, PRN, AUX, NUL, COM0-9, LPT0-9,
 * CONIN$, CONOUT$, CLOCK$) gets a '_' in front; a name that repeats an
 * earlier one, compared case-insensitively, gets "~<entry index>" before its
 * extension.  Members are stored, never compressed.  A file whose granules
 * lie past the end of a truncated image is listed but refuses to unpack.
 */
typedef struct xx_rsdos_fs {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;      /**< Bytes from base_address to the end. */
    uint32_t header_size;    /**< JVC / VDK header bytes, 0 when none. */
    uint32_t container;      /**< 0 headerless, 1 JVC header, 2 VDK. */
    uint32_t sides;          /**< 1 or 2. */
    uint32_t tracks;         /**< Whole tracks per side in the image. */
    uint32_t granule_count;  /**< Granule-table entries in use, 68..255. */
    uint32_t free_granules;  /**< Granule-table entries equal to 0xFF. */
} xx_rsdos_fs;

typedef xx_rsdos_fs xx_rsdos_fs_t;

XXFC_API void xx_rsdos_fs_init(xx_rsdos_fs *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_rsdos_fs *xx_rsdos_fs_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_rsdos_fs_destroy(xx_rsdos_fs *archive);
XXFC_API void xx_rsdos_fs_free(xx_rsdos_fs *archive);

XXFC_API bool xx_rsdos_fs_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_rsdos_fs_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_rsdos_fs_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_rsdos_fs_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rsdos_fs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rsdos_fs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rsdos_fs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rsdos_fs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rsdos_fs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RSDOS_FS_H */
