/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_7_setup.h
 *  @brief InstallShield 7.x "Setup Player 2K2" single-exe (All-in-One). */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield "All-in-One" single-file setup.exe (U3 "SFX IS14",
 * XArchive FT_IS14_SFX, IsXunpack "All-in-One modification").
 *
 * The file is an ordinary PE32 loader stub.  The payload is the PE overlay:
 * it starts right behind the raw data of the last section and holds a chain
 * of records running to end of file, with no header, no count and no
 * trailer.  Each record is
 *
 *   char[] base name         NUL-terminated ANSI, e.g. "data1.cab"
 *   char[] relative path     NUL-terminated ANSI, e.g. "Disk1\\data1.cab";
 *                            its last component is the base name
 *   char[] version           NUL-terminated dotted decimal, "0.0.0.0" for
 *                            unversioned files, "7.1.100.1248" for setup.exe
 *   char[] size              NUL-terminated decimal byte count
 *   u8[size] data            stored, never compressed
 *
 * Members are the Disk1 tree of an InstallShield 7.x release: data1.hdr /
 * dataN.cab (ISc( cabinets), engine32.cab (MSCF), setup.inx, setup.boot,
 * setup.skin, layout.bin, setup.exe, setup.ini, patches (.RTP), bitmaps.
 *
 * The reader accepts the file only when the whole chain parses and its last
 * record ends exactly at end of file, or exactly where an Authenticode
 * certificate table that runs to end of file begins (up to 7 zero bytes of
 * alignment in between).  Member names are the relative paths with '\\'
 * turned into '/', bytes 0x80-0xFF and '%' written as "%XX"; a name that a
 * case-insensitive file system would merge with an earlier one gets "%_<n>"
 * (n = record index) inserted before its extension.
 */
typedef struct xx_installshield_7_setup {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset; /**< Overlay start, relative to base_address. */
    int64_t payload_end;    /**< Where the record chain ends (relative). */
} xx_installshield_7_setup;

typedef xx_installshield_7_setup xx_installshield_7_setup_t;

XXFC_API void xx_installshield_7_setup_init(xx_installshield_7_setup *archive,
                                            xx_io_device *device,
                                            int64_t base_address);
XXFC_API xx_installshield_7_setup *xx_installshield_7_setup_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_7_setup_destroy(
    xx_installshield_7_setup *archive);
XXFC_API void xx_installshield_7_setup_free(xx_installshield_7_setup *archive);

XXFC_API bool xx_installshield_7_setup_check_is_valid(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API bool xx_installshield_7_setup_handle_base_info(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_7_setup_get_format_size(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_7_setup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_7_setup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_7_setup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_7_setup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_7_setup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_7_setup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_7_SETUP_H */
