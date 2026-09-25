/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_7_setup2.h
 *  @brief InstallShield 7.x setup.boot (the Setup.exe engine bundle). */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP2_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield 7.x "setup.boot" file (U3 "IS7 BOOT").
 *
 * InstallShield 7's Setup.exe loader reads the engine DLLs it needs from
 * setup.boot, next to it on Disk1.  The file has no header, no count and no
 * trailer: it is a chain of records starting at offset 0.  Each record is
 *
 *   char[] short name     NUL-terminated ANSI, the compressed name,
 *                         e.g. "setup.dl_", "_setup7.dl_", "igdi.dl_"
 *   char[] long name      NUL-terminated ANSI, the installed name,
 *                         e.g. "Setup.dll", "_Setup.dll", "IGdi.dll"
 *   char[] version        NUL-terminated dotted decimal ("7.1.100.1248"),
 *                         possibly empty
 *   char[] size           NUL-terminated decimal byte count of the member
 *   u8[size] member       a complete Microsoft COMPRESS (SZDD) file:
 *                         "SZDD\x88\xf0\x27\x33", method 'A', the missing
 *                         name character, u32 LE unpacked size, LZSS data
 *
 * Every known file starts with the "setup.dl_" / "Setup.dll" record, which
 * is what the detector keys on.
 *
 * A record is accepted when its four strings parse, its member lies inside
 * the device and begins with an SZDD 'A' header whose unpacked size the
 * LZSS data can reach.  The chain ends at end of data, at the first offset
 * where no such record starts, or after 1024 records; the format size is
 * where it ended.
 * Members are the SZDD streams decoded, named by the long name with '\\'
 * turned into '/' and bytes 0x80-0xFF and '%' written as "%XX"; a name that
 * a case-insensitive file system would merge with an earlier one gets
 * "%_<n>" (n = record index) inserted before its extension.
 */
typedef struct xx_installshield_7_setup2 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t chain_end; /**< Where the record chain ends (relative). */
} xx_installshield_7_setup2;

typedef xx_installshield_7_setup2 xx_installshield_7_setup2_t;

XXFC_API void xx_installshield_7_setup2_init(
    xx_installshield_7_setup2 *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_installshield_7_setup2 *xx_installshield_7_setup2_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_7_setup2_destroy(
    xx_installshield_7_setup2 *archive);
XXFC_API void xx_installshield_7_setup2_free(
    xx_installshield_7_setup2 *archive);

XXFC_API bool xx_installshield_7_setup2_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_installshield_7_setup2_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_7_setup2_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_7_setup2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_7_setup2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_7_setup2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_7_setup2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_7_setup2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_7_setup2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP2_H */
