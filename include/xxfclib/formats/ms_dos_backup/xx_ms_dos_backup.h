/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ms_dos_backup.h @brief MS-DOS 2.0-3.2 BACKUP file reader. */

#ifndef XXFCLIB_FORMAT_MS_DOS_BACKUP_H
#define XXFCLIB_FORMAT_MS_DOS_BACKUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One file written by MS-DOS 2.0-3.2 BACKUP.EXE / BACKUP.COM.
 *
 * Those versions copy every backed-up file to the backup disk under its own
 * name, prefixed with a 128-byte header; a file that spans several disks is
 * split into fragments, one per disk, each with its own header.
 *
 *   0x00  u8       0xFF on the last fragment of the file, 0x00 otherwise
 *   0x01  u16 LE   fragment (disk) sequence number, 1-based
 *   0x03  u16 LE   0
 *   0x05  char[78] full original path without the drive, e.g. "\DIR\F.TXT",
 *                  code page 437, NUL terminated and zero padded
 *   0x53  u8       path length counting its NUL (3..78)
 *   0x54  44 zero bytes
 *   0x80  the file data (this fragment's part of it), to the end of the file
 *
 * The reader exposes one record: the data after the header.  A complete file
 * (sequence 1 and last-fragment marker set) keeps its original path; any
 * other fragment gets ".NNN" (its sequence number) appended to the path, as
 * one fragment alone is not the whole file.
 */
typedef struct xx_ms_dos_backup {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t sequence;   /**< u16 at 0x01. */
    bool last_fragment;  /**< byte 0 == 0xFF. */
    int64_t data_size;   /**< Bytes after the 128-byte header. */
} xx_ms_dos_backup;

typedef xx_ms_dos_backup xx_ms_dos_backup_t;

#define XX_MS_DOS_BACKUP_HEADER_SIZE 128

XXFC_API void xx_ms_dos_backup_init(xx_ms_dos_backup *archive,
                                    xx_io_device *device,
                                    int64_t base_address);
XXFC_API xx_ms_dos_backup *xx_ms_dos_backup_create(xx_io_device *device,
                                                   int64_t base_address);
XXFC_API void xx_ms_dos_backup_destroy(xx_ms_dos_backup *archive);
XXFC_API void xx_ms_dos_backup_free(xx_ms_dos_backup *archive);

XXFC_API bool xx_ms_dos_backup_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_ms_dos_backup_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_ms_dos_backup_get_format_size(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API uint64_t xx_ms_dos_backup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ms_dos_backup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ms_dos_backup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ms_dos_backup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ms_dos_backup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ms_dos_backup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ms_dos_backup_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ms_dos_backup_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ms_dos_backup_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ms_dos_backup_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ms_dos_backup_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_MS_DOS_BACKUP_H */
