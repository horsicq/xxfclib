/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ms_dos_backup2.h @brief MS-DOS 3.3-5.x BACKUP set (CONTROL.nnn + BACKUP.nnn). */

#ifndef XXFCLIB_FORMAT_MS_DOS_BACKUP2_H
#define XXFCLIB_FORMAT_MS_DOS_BACKUP2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One MS-DOS 3.3-5.x BACKUP set, read through its CONTROL files.
 *
 * BACKUP.COM of MS-DOS 3.3 up to 5.x writes two files per target volume:
 * CONTROL.nnn, the catalogue, and BACKUP.nnn, the member bytes stored
 * back-to-back with no header.  The reader is opened on a CONTROL file; the
 * catalogue alone gives names, sizes, attributes and times.  Extraction
 * needs the matching BACKUP file(s): attach them with
 * xx_ms_dos_backup2_set_data_device / _add_volume, or let
 * xx_ms_dos_backup2_open_volume_files find them next to the CONTROL file.
 * When nothing was attached and the reader's device is a plain file opened
 * by path at offset 0, the first catalogue read calls
 * xx_ms_dos_backup2_open_volume_files on that path itself.
 * Without data a member is still listed, and extracting it fails.
 *
 * A CONTROL file of volume 2 or later may begin with the continuation of a
 * file split at the end of the volume before it.  That member is completed
 * from the earlier volumes (xx_ms_dos_backup2_add_prior_volume, or found by
 * xx_ms_dos_backup2_open_volume_files); their other members are not listed.
 *
 * CONTROL.nnn, all integers little endian:
 *
 *   header, 139 bytes
 *     0x00  u8       0x8B, the header length
 *     0x01  char[8]  "BACKUP  "
 *     0x09  u8       volume sequence number, 1-based
 *     0x0A  128 bytes reserved
 *     0x8A  u8       0xFF on the last volume of the set, 0x00 otherwise
 *
 *   then, repeated: one directory item followed by its file items
 *
 *   directory item, 70 bytes
 *     +0x00  u8        0x46, the item length
 *     +0x01  char[63]  path without drive and leading '\', NUL padded
 *                      ("" for the root, "DOS\UTIL" below it)
 *     +0x40  u16       number of file items that follow
 *     +0x42  u32       offset of the next directory item, 0xFFFFFFFF on
 *                      the last one
 *
 *   file item, 34 bytes
 *     +0x00  u8        0x22, the item length
 *     +0x01  char[12]  8.3 name with its dot, NUL padded
 *     +0x0D  u8        flags: 0x01 last fragment, 0x02 backed up OK,
 *                      0x04 has extended attributes
 *     +0x0E  u32       size of the whole file
 *     +0x12  u16       fragment number, 1-based
 *     +0x14  u32       offset of this fragment in BACKUP.nnn
 *     +0x18  u32       length of this fragment
 *     +0x1C  u16       DOS attributes
 *     +0x1E  u16       DOS time
 *     +0x20  u16       DOS date
 *
 * A file that did not fit on a volume continues on the next one as
 * fragment 2, 3, ...  The reader joins the fragments of every attached
 * volume into one member; a member whose fragments are not all present, or
 * whose fragment lengths do not add up to its size, is listed but not
 * extracted.  An archive record carries XX_META_ID_ATTRIBUTES,
 * XX_META_ID_LAST_MOD_DATE and XX_META_ID_LAST_MOD_TIME from the file item.
 */

/** Volume numbers are one byte. */
#define XX_MS_DOS_BACKUP2_MAX_VOLUMES 255U

typedef struct xx_ms_dos_backup2 {
    Abstractformat format;
    /** Volumes attached so far; volume 0 is format.device. */
    uint32_t volume_count;
    xx_io_device *control[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    xx_io_device *data[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    bool control_owned[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    bool data_owned[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    uint64_t number_of_records;
    uint32_t sequence;   /**< Volume number of the first CONTROL file. */
    bool last_volume;    /**< The last attached volume closes the set. */
    /** Volumes before the first one, nearest first: prior_control[0] is
     *  volume sequence - 1.  Only used to complete the member the first
     *  CONTROL file continues. */
    uint32_t prior_count;
    xx_io_device *prior_control[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    xx_io_device *prior_data[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    bool prior_control_owned[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    bool prior_data_owned[XX_MS_DOS_BACKUP2_MAX_VOLUMES];
    bool companions_tried; /**< Automatic lookup beside the source done. */
} xx_ms_dos_backup2;

typedef xx_ms_dos_backup2 xx_ms_dos_backup2_t;

XXFC_API void xx_ms_dos_backup2_init(xx_ms_dos_backup2 *archive,
                                     xx_io_device *device,
                                     int64_t base_address);
XXFC_API xx_ms_dos_backup2 *xx_ms_dos_backup2_create(xx_io_device *device,
                                                     int64_t base_address);
XXFC_API void xx_ms_dos_backup2_destroy(xx_ms_dos_backup2 *archive);
XXFC_API void xx_ms_dos_backup2_free(xx_ms_dos_backup2 *archive);

XXFC_API bool xx_ms_dos_backup2_check_is_valid(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API bool xx_ms_dos_backup2_handle_base_info(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API int64_t xx_ms_dos_backup2_get_format_size(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API uint64_t xx_ms_dos_backup2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ms_dos_backup2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ms_dos_backup2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ms_dos_backup2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ms_dos_backup2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ms_dos_backup2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Attach BACKUP.nnn of the first volume (the one format.device is
 * the CONTROL file of).  The device is borrowed and must outlive the reader.
 */
XXFC_API bool xx_ms_dos_backup2_set_data_device(xx_ms_dos_backup2 *archive,
                                                xx_io_device *data);

/**
 * @brief Attach the next volume: its CONTROL and BACKUP devices, borrowed.
 * Its sequence number must follow the previous volume's, and the previous
 * volume must not be marked last.
 */
XXFC_API bool xx_ms_dos_backup2_add_volume(xx_ms_dos_backup2 *archive,
                                           xx_io_device *control,
                                           xx_io_device *data);

/**
 * @brief Attach the volume before the earliest one attached so far (first
 * call: volume sequence - 1 of format.device), CONTROL and BACKUP devices,
 * borrowed.  @p data may be NULL.  Used only to complete the member the
 * first CONTROL file continues from that volume.
 */
XXFC_API bool xx_ms_dos_backup2_add_prior_volume(xx_ms_dos_backup2 *archive,
                                                 xx_io_device *control,
                                                 xx_io_device *data);

/**
 * @brief Open the BACKUP file beside @p control_path (CONTROL.nnn ->
 * BACKUP.nnn in the same directory), then CONTROL/BACKUP of each following
 * volume in that directory until the one marked last or the first missing
 * file.  When the CONTROL file continues a member from the volume before
 * it, the earlier CONTROL/BACKUP files that member needs are opened too.
 * Opened devices are owned by the reader.
 * @return the number of volumes that now have their data attached.
 */
XXFC_API uint32_t xx_ms_dos_backup2_open_volume_files(xx_ms_dos_backup2 *archive,
                                                      const char *control_path);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MS_DOS_BACKUP2_H */
