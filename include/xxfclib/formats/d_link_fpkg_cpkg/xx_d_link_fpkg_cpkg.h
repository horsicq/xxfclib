/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_d_link_fpkg_cpkg.h @brief D-Link FPKG / CPKG firmware package. */

#ifndef XXFCLIB_FORMAT_D_LINK_FPKG_CPKG_H
#define XXFCLIB_FORMAT_D_LINK_FPKG_CPKG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The FPKG / CPKG package of D-Link DFL firewall firmware: a plain
 * archive of stored, named files.  Layout as documented by unblob's
 * handlers/archive/dlink/fpkg.py (MIT); every field is BIG endian:
 *
 *   +0   char[4] magic, "FPKG" or "CPKG"
 *   +4   u32     0x01000000 in every known sample (bytes 01 00 00 00)
 *   +8   u32     first_entry_offset, from the start of the package, >= 12
 *   FPKG only:
 *   +12  u32     unknown, 1 observed
 *   +16  u32     name_len
 *   +20  char    model name[name_len]
 *
 * From first_entry_offset, entries follow back to back:
 *
 *   +0   u32     header_len, always 28
 *   +4   u16     type: 0x100 file, 0x101 (unseen), 0x102 checksum,
 *                0x103 signature
 *   +6   u16     unknown (0 or 0x6874 observed)
 *   +8   u32     file_size
 *   +12  char[16] file name, NUL padded
 *   +28  file_size bytes of data
 *
 * There is no entry count and no total size: the walk runs to the end of
 * the device and stops at the first entry that is not well formed (header
 * length other than 28, unknown type, non-printable name byte, or data past
 * the end of the device).  The package ends after the last good entry, and
 * at least one entry is required.
 *
 * Member names are the stored name with trailing blanks removed, as unblob
 * does.  An empty name becomes "entry_<index>.bin", a repeated name
 * (ASCII case-insensitive) gets "_<n>" before its extension, and unsafe
 * names (absolute, drive, "..", device names, reserved characters) are
 * listed but refused on extraction.
 */

#define XX_D_LINK_FPKG_CPKG_HEADER_SIZE 12U
#define XX_D_LINK_FPKG_CPKG_ENTRY_SIZE 28U
#define XX_D_LINK_FPKG_CPKG_NAME_SIZE 16U
/** Entries beyond this are not walked. */
#define XX_D_LINK_FPKG_CPKG_MAX_RECORDS 65536U
/** Longest FPKG model name kept. */
#define XX_D_LINK_FPKG_CPKG_MAX_MODEL 256U

#define XX_D_LINK_FPKG_CPKG_TYPE_FILE 0x100U
#define XX_D_LINK_FPKG_CPKG_TYPE_UNKNOWN 0x101U
#define XX_D_LINK_FPKG_CPKG_TYPE_CHECKSUM 0x102U
#define XX_D_LINK_FPKG_CPKG_TYPE_SIGNATURE 0x103U

typedef struct xx_d_link_fpkg_cpkg {
    Abstractformat format;
    uint64_t number_of_records;
    bool is_fpkg;                 /**< "FPKG" (true) or "CPKG" (false). */
    uint32_t first_entry_offset;
    int64_t archive_end;          /**< Absolute end of the last entry, or -1. */
    char model[XX_D_LINK_FPKG_CPKG_MAX_MODEL + 1U]; /**< FPKG model, or "". */
} xx_d_link_fpkg_cpkg;

typedef xx_d_link_fpkg_cpkg xx_d_link_fpkg_cpkg_t;

XXFC_API void xx_d_link_fpkg_cpkg_init(xx_d_link_fpkg_cpkg *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_d_link_fpkg_cpkg *xx_d_link_fpkg_cpkg_create(xx_io_device *device,
                                                         int64_t base_address);
XXFC_API void xx_d_link_fpkg_cpkg_destroy(xx_d_link_fpkg_cpkg *archive);
XXFC_API void xx_d_link_fpkg_cpkg_free(xx_d_link_fpkg_cpkg *archive);

XXFC_API bool xx_d_link_fpkg_cpkg_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_d_link_fpkg_cpkg_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_d_link_fpkg_cpkg_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_d_link_fpkg_cpkg_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_d_link_fpkg_cpkg_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_d_link_fpkg_cpkg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_d_link_fpkg_cpkg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_d_link_fpkg_cpkg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_d_link_fpkg_cpkg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_D_LINK_FPKG_CPKG_H */
