/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_finstall.h @brief "F Install 2" installer disk data reader. */

#ifndef XXFCLIB_FORMAT_FINSTALL_H
#define XXFCLIB_FORMAT_FINSTALL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One disk of an "F Install" installer set (DISK1, DISK2, ...).
 *
 * The installer's floppy data files hold a table of stored (uncompressed)
 * files.  All integers are little-endian; offsets count from the start of
 * the disk file.
 *
 *   0x00  u8      0x01
 *   0x01  char[10] "F Install "
 *   0x0B  char    version digit ('2' in every known disk)
 *   0x0C  u32     capacity the set was split for (1,445,000 in the known
 *                 disk, which is 5,904 bytes larger than that disk); kept
 *                 for information, never trusted
 *   0x10  u16     number of entries, N >= 1
 *   0x12  N entries of 0x18 bytes:
 *           +0x00 u32     data offset
 *           +0x04 u32     data size
 *           +0x08 char[16] DOS file name, NUL terminated unless all 16
 *                          bytes are used (code page 437)
 *   the data of the entries, in directory order
 *
 * The first entry's data starts right after the directory, and every later
 * entry starts at or after the end of the one before it (in the known disk
 * they are packed back to back and the last ends at the end of the file).
 */
typedef struct xx_finstall {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t capacity;   /**< The u32 at 0x0C. */
    char version;        /**< The digit at 0x0B. */
    int64_t data_end;    /**< End of the last entry's data, from the base. */
    bool truncated;      /**< Some entry's data runs past the device end. */
} xx_finstall;

typedef xx_finstall xx_finstall_t;

/** Size of the fixed header before the directory. */
#define XX_FINSTALL_HEADER_SIZE 0x12
/** Size of one directory entry. */
#define XX_FINSTALL_ENTRY_SIZE 0x18
/** Length of the name field inside a directory entry. */
#define XX_FINSTALL_NAME_FIELD 16

XXFC_API void xx_finstall_init(xx_finstall *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_finstall *xx_finstall_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_finstall_destroy(xx_finstall *archive);
XXFC_API void xx_finstall_free(xx_finstall *archive);

XXFC_API bool xx_finstall_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_finstall_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_finstall_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_finstall_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_finstall_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_finstall_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_finstall_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_finstall_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_finstall_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FINSTALL_H */
