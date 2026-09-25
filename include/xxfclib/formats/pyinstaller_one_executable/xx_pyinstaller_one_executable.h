/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/**
 * @file xx_pyinstaller_one_executable.h
 * @brief PyInstaller one-file executable: the bootloader with PyInstaller's
 * own "CArchive" package appended to it.
 *
 * The package is found from the END of the file.  Its last bytes are the
 * cookie (all fields big-endian):
 *
 *   0x00  char[8]   "MEI\x0c\x0b\x0a\x0b\x0e"
 *   0x08  uint32    package length, cookie included
 *   0x0C  uint32    TOC offset, relative to the package start
 *   0x10  uint32    TOC length
 *   0x14  int32     Python version (27, 36, 310, 314, ...)
 *   0x18  char[64]  Python library name, e.g. "python314.dll"
 *                   (PyInstaller 2.1 and later; the 2.0 cookie stops at 0x18)
 *
 * Package start = cookie end - package length.  In a PE bootloader that is
 * exactly the overlay.  The TOC ends where the cookie begins, so the
 * cookie size is package length - TOC offset - TOC length and has to come
 * out as 88 or 24.  The cookie either ends the file or, in a signed
 * executable, sits right in front of the Authenticode blob that the PE
 * security directory points at.
 *
 * TOC entry (big-endian):
 *
 *   0x00  int32     entry length, name padding included
 *   0x04  uint32    data offset, relative to the package start
 *   0x08  uint32    stored length
 *   0x0C  uint32    unpacked length
 *   0x10  uint8     0 = stored, 1 = one zlib (RFC 1950) stream
 *   0x11  char      type: b binary, x data, z/Z PYZ or zip, m/M module or
 *                   package, s script, l splash, n symlink target,
 *                   o runtime option, d dependency
 *   0x12  char[]    name, NUL-terminated and NUL-padded, UTF-8, '\\' or '/'
 *
 * Runtime options ('o') and dependency references ('d') are bootloader
 * settings, not files, and are not listed as records.  Every other entry is
 * one record whose bytes are the stored (unpacked) member exactly as the
 * TOC names it: 'm'/'s' members stay the bare marshalled code objects
 * PyInstaller stores, and the 'z' member is the PYZ archive the pyz reader
 * opens.
 */

#ifndef XXFCLIB_FORMAT_PYINSTALLER_ONE_EXECUTABLE_H
#define XXFCLIB_FORMAT_PYINSTALLER_ONE_EXECUTABLE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PyInstaller one-file executable and its CArchive package.
 */
typedef struct xx_pyinstaller_one_executable {
    Abstractformat format;
    uint64_t number_of_records;  /**< TOC entries that are files. */
    uint64_t number_of_entries;  /**< All TOC entries, options included. */
    int64_t package_offset;      /**< Package start, relative to base. */
    int64_t cookie_offset;       /**< Cookie start, relative to base. */
    uint32_t cookie_size;        /**< 88, or 24 for PyInstaller 2.0. */
    uint32_t python_version;     /**< The cookie's Python version field. */
    char python_library[65];     /**< The cookie's library name, or "". */
} xx_pyinstaller_one_executable;

typedef xx_pyinstaller_one_executable xx_pyinstaller_one_executable_t;

XXFC_API void xx_pyinstaller_one_executable_init(
    xx_pyinstaller_one_executable *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_pyinstaller_one_executable *xx_pyinstaller_one_executable_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_pyinstaller_one_executable_destroy(
    xx_pyinstaller_one_executable *archive);
XXFC_API void xx_pyinstaller_one_executable_free(
    xx_pyinstaller_one_executable *archive);

XXFC_API bool xx_pyinstaller_one_executable_check_is_valid(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API bool xx_pyinstaller_one_executable_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pyinstaller_one_executable_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_pyinstaller_one_executable_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_pyinstaller_one_executable_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_pyinstaller_one_executable_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pyinstaller_one_executable_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pyinstaller_one_executable_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pyinstaller_one_executable_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PYINSTALLER_ONE_EXECUTABLE_H */
