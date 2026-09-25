/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sfx_nullsoft_pimp.h
 * @brief Nullsoft PiMP (Plug-in Mini Packager, 1999-2000) installers.
 *
 * PiMP ("nspimpsystem") is the installer Nullsoft used for Winamp plug-ins
 * before NSIS.  A package is a small PE stub (it links inflate 1.1.3) with
 * the payload appended at the exact end of the last section's raw data.
 * All offsets below are relative to that overlay; integers are
 * little-endian.
 *
 *   +0x000  8      "PIMPFILE"
 *   +0x008  1      install-directory flag; 0 = the next field is absent
 *   +0x009  0x104  default install directory, NUL-padded (flag != 0 only)
 *   B       0x80   title, NUL-padded
 *   B+0x80  0x80   description, NUL-padded
 *
 * Layout 2 (every known package):
 *   B+0x100 4      unknown, zero in the known packages
 *   B+0x104 4      member count
 *   B+0x108        members: u32 name size (terminating NUL included),
 *                  name, u32 packed size, u32 unpacked size, and a zlib
 *                  stream (RFC 1950, Adler-32 trailer) of the packed size
 *
 * Layout 1 (older packages, as U3 reads them; no known sample):
 *   B+0x100 4      member count
 *   B+0x104        members: u32 name size, name, u32 packed size, zlib
 *
 * The members are followed by a u32 command size and the post-install
 * command line (NUL-terminated, e.g. |"$WINDIR\notepad.exe" whatsnew.txt).
 * That command normally ends the file.
 *
 * Member names carry installer variables such as "$INSTDIR\" or
 * "$VISDIR\"; they are listed with '/' separators and the variable kept as
 * the first folder.
 */

#ifndef XXFCLIB_FORMAT_SFX_NULLSOFT_PIMP_H
#define XXFCLIB_FORMAT_SFX_NULLSOFT_PIMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest title / description / install directory after conversion to
 * UTF-8 (every stored byte may take up to three bytes). */
#define XX_SFX_NULLSOFT_PIMP_TEXT_MAX (0x80 * 3)
#define XX_SFX_NULLSOFT_PIMP_DIR_MAX (0x104 * 3)

typedef struct xx_sfx_nullsoft_pimp {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t overlay_offset;   /**< "PIMPFILE", relative to the base. */
    int64_t directory_offset; /**< First member header. */
    int64_t command_offset;   /**< The u32 command size. */
    uint32_t command_size;    /**< Terminating NUL included. */
    uint32_t layout;          /**< 1 or 2, see above. */
    bool has_install_dir;
    char title[XX_SFX_NULLSOFT_PIMP_TEXT_MAX + 1];
    char description[XX_SFX_NULLSOFT_PIMP_TEXT_MAX + 1];
    char install_dir[XX_SFX_NULLSOFT_PIMP_DIR_MAX + 1];
} xx_sfx_nullsoft_pimp;

typedef xx_sfx_nullsoft_pimp xx_sfx_nullsoft_pimp_t;

XXFC_API void xx_sfx_nullsoft_pimp_init(xx_sfx_nullsoft_pimp *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_sfx_nullsoft_pimp *xx_sfx_nullsoft_pimp_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_nullsoft_pimp_destroy(xx_sfx_nullsoft_pimp *archive);
XXFC_API void xx_sfx_nullsoft_pimp_free(xx_sfx_nullsoft_pimp *archive);

XXFC_API bool xx_sfx_nullsoft_pimp_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_sfx_nullsoft_pimp_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_nullsoft_pimp_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_nullsoft_pimp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_nullsoft_pimp_create_archive_records_reading(Abstractformat *self,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_nullsoft_pimp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_nullsoft_pimp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_nullsoft_pimp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_nullsoft_pimp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_NULLSOFT_PIMP_H */
