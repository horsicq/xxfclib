/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_acorn_atom_disk.h @brief Acorn Atom DOS disk image reader. */

#ifndef XXFCLIB_FORMAT_ACORN_ATOM_DISK_H
#define XXFCLIB_FORMAT_ACORN_ATOM_DISK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Acorn Atom disk image (.40t / .dsk) holding an Atom DOS
 *        filesystem.
 *
 * Image: a headerless dump of a single-sided 5.25" FM disk, 40 tracks of
 * 10 sectors of 256 bytes, track after track, sectors in ID order (IDs start
 * at 0).  That is MAME's "atom" format and it has exactly one size, 102400
 * bytes.  Logical sector S lives at image offset S * 256.
 *
 * Atom DOS is the filing system Acorn DFS was derived from; its catalogue is
 * the DFS one, in logical sectors 0 and 1:
 *   sector 0, bytes 0..7    disc title, first part
 *   sector 0, 8*i .. +6     file i name, 7 characters padded with spaces
 *   sector 0, 8*i + 7       file i qualifier (low 7 bits), bit 7 = locked
 *   sector 1, bytes 0..3    disc title, second part
 *   sector 1, byte 4        write cycle count
 *   sector 1, byte 5        number of files * 8 (files 1..31)
 *   sector 1, byte 6        bits 0-1 = sectors on disc b9..b8,
 *                           bits 4-5 = boot option
 *   sector 1, byte 7        sectors on disc b7..b0
 *   sector 1, 8*i + 0..1    load address (little endian)
 *   sector 1, 8*i + 2..3    execution address
 *   sector 1, 8*i + 4..5    length b15..b0
 *   sector 1, 8*i + 6       bits 0-1 start sector b9..b8, 2-3 load b17..b16,
 *                           4-5 length b17..b16, 6-7 exec b17..b16
 *   sector 1, 8*i + 7       start sector b7..b0
 * A file occupies ceil(length / 256) consecutive sectors from its start.
 *
 * Detection: no magic, so this is a late probe.  The image must be exactly
 * 102400 bytes; the file count byte must be a multiple of 8 giving 1..31
 * files; the disc sector count must be 2..400; the Atom-specific bits must be
 * clear (boot option, and the load / exec b17..b16 bits of every file, since
 * the Atom has a 16-bit address space: files written by a BBC Micro DFS
 * normally carry &FF host-address bits there); every file must have a name of
 * printable ASCII without bit 7 and with padding only at the end, a printable
 * qualifier, a start sector >= 2 with its whole extent inside the disc sector
 * count, and no two files may share a sector.  A blank or freshly formatted
 * disk (no files) is not claimed.  The title bytes are not checked.
 *
 * Members are the files in catalogue order.  The name is "NAME" for the
 * default (space) qualifier and "Q.NAME" otherwise.  Characters / \ : * ? "
 * < > | become '_', as do a trailing '.' and a '.' qualifier; a Windows device stem (CON, PRN,
 * AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$) gets a '_' in front.  A
 * name repeating an earlier one, compared case-insensitively, gets
 * "~<catalogue index>" appended, so every member name is unique.
 * XX_META_ID_ATTRIBUTES is 1 for a locked file, else 0.
 */
typedef struct xx_acorn_atom_disk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;      /**< Always 102400. */
    uint32_t disc_sectors;   /**< Sector count from the catalogue. */
    uint32_t cycle;          /**< Catalogue write cycle byte. */
    char title[13];          /**< Title, trailing spaces / NULs removed. */
} xx_acorn_atom_disk;

typedef xx_acorn_atom_disk xx_acorn_atom_disk_t;

XXFC_API void xx_acorn_atom_disk_init(xx_acorn_atom_disk *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_acorn_atom_disk *xx_acorn_atom_disk_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_acorn_atom_disk_destroy(xx_acorn_atom_disk *archive);
XXFC_API void xx_acorn_atom_disk_free(xx_acorn_atom_disk *archive);

XXFC_API bool xx_acorn_atom_disk_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_acorn_atom_disk_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_acorn_atom_disk_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_acorn_atom_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_acorn_atom_disk_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_acorn_atom_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_acorn_atom_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_acorn_atom_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_acorn_atom_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ACORN_ATOM_DISK_H */
