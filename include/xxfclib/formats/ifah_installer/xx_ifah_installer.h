/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ifah_installer.h @brief "IFAH" installer package (setup.exe). */

#ifndef XXFCLIB_FORMAT_IFAH_INSTALLER_H
#define XXFCLIB_FORMAT_IFAH_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The package of an installer of unidentified vendor, named here
 * after its signature: a 64 KiB UPX-packed PE stub (zlib 1.1.3 inside)
 * whose overlay is an "IFAH" header and a chain of "IFFH" file records.
 *
 * Little endian throughout.  Package header, 17 bytes:
 *
 *   +0x00  char[4]  "IFAH"
 *   +0x04  u32      size of the whole setup file, stub included
 *   +0x08  u32      not interpreted (FF FF FF FF in every known package)
 *   +0x0C  u8       not interpreted (1 in every known package)
 *   +0x0D  u32      number of records (>= 1)
 *
 * then that many records, back to back, each:
 *
 *   +0x00  char[4]  "IFFH"
 *   +0x04  u32      unpacked size (< 2^31)
 *   +0x08  u32      packed size (< 2^31)
 *   +0x0C  u32      CRC-32 (ISO-HDLC, as zlib) of the unpacked bytes
 *   +0x10  u16      DOS time
 *   +0x12  u16      DOS date
 *   +0x14  u32 x2   file version, most significant half first
 *                   (all FF: the file carries none)
 *   +0x1C  u8       name length N (>= 1)
 *   +0x1D  char[N]  name, '\\' separators, no terminator
 *   then            the packed bytes: one raw deflate stream
 *                   (zlib windowBits -15)
 *
 * The first two records of every known package are the installer's own
 * ("IFII" install information, "IFWB" wizard bitmap); they are listed and
 * extracted like any other record.  In the known packages the chain ends
 * exactly at the size the header gives, which is the end of the file.
 *
 * Where the package is found: at the reader's base address itself (a bare
 * package, as a format search finds it), or as the overlay of a PE image at
 * the base address - the end of the section whose raw data ends last.  In
 * the second case the header's size must equal the distance from the base
 * address to the end of the last record; in the first it must be at least
 * the package's own length.  The executable is parsed only that far; its
 * code is never looked at.
 *
 * Names: '\\' and '/' become '/', bytes outside 0x20..0x7E and '%' become
 * "%XX" (upper-case hex), so the conversion is reversible and code-page
 * free.  A name that repeats an earlier one (compared without ASCII case)
 * gets "%_<record index>" inserted before its extension; "%_" never occurs
 * in a converted name otherwise.  A name with a control character, an
 * empty, "." or ".." component, a component ending in '.' or ' ', a
 * Windows device name or one of : < > " | ? * is listed but refused on
 * extraction.
 *
 * Extraction checks the unpacked size and the CRC-32 and applies the DOS
 * time stamp (best effort).
 */
typedef struct xx_ifah_installer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset; /**< Absolute offset of the "IFAH" header. */
    int64_t payload_end;    /**< Absolute end of the last record. */
    uint32_t declared_size; /**< The header's u32 at +0x04. */
    bool is_sfx;            /**< Found as a PE overlay. */
} xx_ifah_installer;

typedef xx_ifah_installer xx_ifah_installer_t;

XXFC_API void xx_ifah_installer_init(xx_ifah_installer *archive,
                                     xx_io_device *device,
                                     int64_t base_address);
XXFC_API xx_ifah_installer *xx_ifah_installer_create(xx_io_device *device,
                                                     int64_t base_address);
XXFC_API void xx_ifah_installer_destroy(xx_ifah_installer *archive);
XXFC_API void xx_ifah_installer_free(xx_ifah_installer *archive);

XXFC_API bool xx_ifah_installer_check_is_valid(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API bool xx_ifah_installer_handle_base_info(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API int64_t xx_ifah_installer_get_format_size(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API uint64_t xx_ifah_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_ifah_installer_create_archive_records_reading(Abstractformat *self,
                                                 const xx_list_s *options,
                                                 xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ifah_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ifah_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ifah_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ifah_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IFAH_INSTALLER_H */
