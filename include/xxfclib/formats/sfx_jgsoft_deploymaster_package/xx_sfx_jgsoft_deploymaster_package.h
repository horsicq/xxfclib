/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_jgsoft_deploymaster_package.h
 *  @brief JGsoft DeployMaster 2.x setup package (Delphi PE stub with the
 *         installation data in its overlay). */

#ifndef XXFCLIB_FORMAT_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_H
#define XXFCLIB_FORMAT_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DeployMaster 2.x "Deployment Package" (Setup.exe).
 *
 * The executable is a small Delphi stub.  Its overlay (the first byte
 * behind the last section's raw data) holds, back to back:
 *
 *   bzip2 stream          the setup engine the stub unpacks to a temporary
 *                         file and runs ("Deploy.exe" in 2.0, "dpy.exe" in
 *                         2.7); it has no length field and ends at the
 *                         byte that closes the stream's end-of-stream
 *                         marker and CRC
 *   u32 0xFFFFFFFF, u32 n, zlib[n]   the user-interface string table
 *                                    (CRLF-separated lines)
 *   u32 n, zlib[n]        the project strings, each closed by 0x0C:
 *                         [0] company, [1] URL, [2] product, [3] URL,
 *                         [4] version, [5] ..., [6] copyright,
 *                         [7] icon file, [8] readme file, [9] licence file,
 *                         [10] setup DLL, then folders and uninstall text;
 *                         an unused file name is an empty field
 *   raw settings          69 bytes in 2.0, 110 in 2.7 (a dialog font
 *                         block was added); not self-describing, so the
 *                         reader moves to the next record by finding it
 *   u32 n, zlib[n]        the wizard font settings
 *   u32 n, zlib[n]        one record per non-empty name of fields 7..10,
 *                         in that order: the "special" files
 *   u8 c, c x (u32 n, zlib[n])   the components
 *   u32 n, zlib[n]        the file list: one CRLF-terminated name per
 *                         installed file
 *   file table            N = specials + listed files entries, stored as
 *                         arrays: u32 offset[N] (0xFFFFFFFF for a special,
 *                         whose data is the inline record above; otherwise
 *                         the offset of that file's record from the start
 *                         of the executable), u32 DOS date/time[N],
 *                         u64 file version[N], u32 size[N], u32 CRC-32[N]
 *   shortcuts, registry, file types, uninstall settings (not needed)
 *   u32 n, zlib[n]        one record per installed file, where the table
 *                         points
 *
 * An Authenticode certificate may follow the last record.  The format size
 * runs to the end of the last file record, or of such a certificate right
 * behind it (to the end of the device when no file record is present or a
 * record is cut off).
 *
 * Members: the setup engine as "Deploy.exe", then every table entry that
 * has data in this file, named from the project strings (specials) and the
 * file list.  Each file is checked against the table's size and CRC-32 and
 * the zlib Adler-32; an entry whose record is cut off (a truncated package)
 * is still listed and fails to unpack.  A package whose settings cannot be
 * walked (another DeployMaster version) is still listed, as the engine plus
 * the rest of the overlay under the name "package.dat".
 */
typedef struct xx_sfx_jgsoft_deploymaster_package {
    Abstractformat format;
    int64_t overlay_start;     /**< Offset of the bzip2 stream, from base. */
    int64_t engine_size;       /**< Packed size of the bzip2 stream. */
    int64_t data_end;          /**< Where the package data ends (a trailing
                                    certificate excluded), from base. */
    int64_t table_offset;      /**< Offset of the file table, -1 if the
                                    settings could not be walked. */
    uint32_t specials;         /**< Special files (icon, readme, ...). */
    uint32_t listed_files;     /**< Names in the file list. */
    uint32_t missing_files;    /**< Table entries without data here
                                    (offset 0xFFFFFFFF). */
    uint32_t damaged_files;    /**< Entries whose record is cut off or
                                    unreadable: listed, fail to unpack. */
    uint64_t number_of_records;
    bool walked;               /**< False: the fallback two-member view. */
} xx_sfx_jgsoft_deploymaster_package;

typedef xx_sfx_jgsoft_deploymaster_package
    xx_sfx_jgsoft_deploymaster_package_t;

XXFC_API void xx_sfx_jgsoft_deploymaster_package_init(
    xx_sfx_jgsoft_deploymaster_package *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_jgsoft_deploymaster_package *
xx_sfx_jgsoft_deploymaster_package_create(xx_io_device *device,
                                          int64_t base_address);
XXFC_API void xx_sfx_jgsoft_deploymaster_package_destroy(
    xx_sfx_jgsoft_deploymaster_package *archive);
XXFC_API void xx_sfx_jgsoft_deploymaster_package_free(
    xx_sfx_jgsoft_deploymaster_package *archive);

XXFC_API bool xx_sfx_jgsoft_deploymaster_package_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfx_jgsoft_deploymaster_package_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_jgsoft_deploymaster_package_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t
xx_sfx_jgsoft_deploymaster_package_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_jgsoft_deploymaster_package_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_jgsoft_deploymaster_package_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool
xx_sfx_jgsoft_deploymaster_package_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_jgsoft_deploymaster_package_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void
xx_sfx_jgsoft_deploymaster_package_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_H */
