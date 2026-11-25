/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_trs_80_jv1.h @brief TRS-80 JV1 disk image reader (TRSDOS/LDOS). */

#ifndef XXFCLIB_FORMAT_TRS_80_JV1_H
#define XXFCLIB_FORMAT_TRS_80_JV1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TRS-80 Model I JV1 disk image (.jv1, .dsk) with a TRSDOS 2.3 /
 * LDOS 5 style filesystem.
 *
 * Image: no header, no signature.  The sectors of a single-sided, single
 * density disk, 10 sectors of 256 bytes per track (IDs 0..9), tracks in
 * order, so sector S of track T is at offset (T * 10 + S) * 256.  The size
 * is therefore a multiple of 2560; the reader accepts 2..96 tracks.
 *
 * Filesystem (Model I layout, as documented by the TRSDOS 2.3 / LDOS 5.1
 * technical references and implemented by Lawrence Kesteloot's MIT-licensed
 * trs80-base Trsdos.ts, which was used to understand it):
 *   - byte 2 of track 0 sector 0 (boot sector), low 7 bits: directory track.
 *   - directory track sector 0: GAT.  +0xD0 disk name (8), +0xD8 date (8),
 *     +0xE0 auto command; each is printable ASCII up to a 0x00 or 0x0D.
 *   - sector 1: HIT, one byte per directory entry code (DEC): 0 = unused,
 *     otherwise the hash of the entry's 11-character file name.
 *   - sectors 2..9: directory, 8 entries of 32 bytes each.  DEC = entry
 *     index * 32 + (sector - 2).
 *   - a granule is 5 sectors; 2 granules per track.
 * Directory entry, 32 bytes:
 *   0   flags: bit 7 extended entry (FXDE), 6 system, 5 PDS/limits,
 *       4 active, 3 invisible, 2..0 protection level
 *   1   FPDE: month (low nibble) and flags; FXDE: DEC of the previous entry
 *   2   day (bits 7..3), year - 1980 (bits 2..0)
 *   3   EOF offset: bytes used in the last sector (0 = full)
 *   4   logical record length (0 = 256)
 *   5   char[8] name, char[3] extension, space padded
 *   16  u16 update password hash, u16 access password hash
 *   20  u16 LE sector count (ERN)
 *   22  up to 5 extents {track, granule byte}: granule byte bits 7..5 =
 *       first granule on the track, bits 4..0 = granule count - 1.  A track
 *       byte of 0xFF ends the list; 0xFE at +30 means +31 is the DEC of the
 *       next (extended) entry, which continues the list.
 * File size = sector count * 256 + EOF, minus 256 when EOF is non-zero.
 * The sectors of an extent run on past the end of a track into the next.
 *
 * Detection: a late probe, cheapest first: size, directory track inside the
 * image, one 2560-byte directory track read, GAT text fields printable, and
 * every active primary entry valid (printable 11-byte name not starting
 * with a space, HIT byte equal to the name hash, extents inside the image
 * and covering the size, a well formed FXDE chain of at most 64 entries).
 * At least one file must exist; an empty or unformatted disk is not
 * claimed.
 *
 * Members are the files (system files included), in directory order.  The
 * name is "NAME.EXT" ("NAME" when the extension is blank); characters
 * outside printable ASCII and / \ : * ? " < > | ~ . become '_', a Windows
 * device stem gets a '_' in front, and a name repeating an earlier one
 * (case-insensitively) gets "~<DEC>" before the extension.
 * XX_META_ID_ATTRIBUTES carries the flags byte.
 */
typedef struct xx_trs_80_jv1 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;
    uint32_t tracks;
    uint32_t directory_track;
} xx_trs_80_jv1;

typedef xx_trs_80_jv1 xx_trs_80_jv1_t;

XXFC_API void xx_trs_80_jv1_init(xx_trs_80_jv1 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_trs_80_jv1 *xx_trs_80_jv1_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_trs_80_jv1_destroy(xx_trs_80_jv1 *archive);
XXFC_API void xx_trs_80_jv1_free(xx_trs_80_jv1 *archive);

XXFC_API bool xx_trs_80_jv1_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_trs_80_jv1_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_trs_80_jv1_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_trs_80_jv1_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_trs_80_jv1_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trs_80_jv1_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trs_80_jv1_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trs_80_jv1_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_trs_80_jv1_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_trs_80_jv1_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_trs_80_jv1_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_trs_80_jv1_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_trs_80_jv1_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_trs_80_jv1_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_TRS_80_JV1_H */
