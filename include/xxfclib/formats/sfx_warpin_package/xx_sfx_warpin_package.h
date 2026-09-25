/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_warpin_package.h @brief WarpIN installer package (.wpi). */

#ifndef XXFCLIB_FORMAT_SFX_WARPIN_PACKAGE_H
#define XXFCLIB_FORMAT_SFX_WARPIN_PACKAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WarpIN package: the archive of the OS/2 / eComStation / ArcaOS
 * "WarpIN" installer.
 *
 * Little endian throughout.  All offsets below are from the package start
 * (the reader's base address).
 *
 *   0x0000  u32   0xBE020477 (bytes 77 04 02 BE)
 *   0x0004  u16   format revision: 3 in every known package, 0..4 accepted
 *   0x0006  512 bytes of descriptive strings (zero in every known package)
 *   0x0206  u16   application revision
 *   0x0208  u16   OS code
 *   0x020A  u16   package count (>= 1)
 *   0x020C  u16   install script size, unpacked (low 16 bits)
 *   0x020E  u16   install script size, packed
 *   0x0210  i32   size of an optional blob after the script (>= 0)
 *   0x0214  revision 4 only: an extension header whose first i32 is its own
 *           size (>= 0x28), skipped whole
 *   then    the install script: one bzip2 stream
 *   then    the optional blob
 *   then    the package table, 0x30 bytes per package:
 *             +0x00 u16 id   +0x02 u16 member count   +0x04 i32 data offset
 *             +0x08 i32 unpacked total   +0x0C i32 packed total
 *             +0x10 char[0x20] label ("Pck001", ...)
 *   then    the members, package after package, each a 0x11D-byte header
 *           and its data:
 *             +0x000 u16 0xF012   +0x002 u16 (not interpreted)
 *             +0x004 u16 method: 0 stored, 1 bzip2
 *             +0x006 u16 package id
 *             +0x008 i32 unpacked size   +0x00C i32 packed size
 *             +0x010 u32 checksum (not a CRC-32 of either form; not checked)
 *             +0x014 char[0x100] name, NUL terminated, '\\' separators;
 *                    bytes after the NUL are uninitialised writer memory
 *             +0x114 u32 last write (Unix time)   +0x118 u32 creation time
 *             +0x11C u8  extension size, always 0
 *
 * The first package's data offset must be the end of the package table;
 * later packages must start at or after the end of the previous one.  A
 * package embedded in a larger file (the self-installing form: an OS/2 stub,
 * then the package) may give its data offsets from the start of that file
 * instead; the first package's offset tells which, and every later offset
 * is read the same way.
 *
 * Records: the install script first, as "install.wis" (its packed and
 * unpacked sizes are the header's), then every member in file order.  A
 * zero-byte member has no stream at all whatever its method says.  The
 * member's Unix last-write time is published as XX_META_ID_TIMESTAMP.
 *
 * Names: '\\' and '/' become '/', bytes outside 0x20..0x7E and '%' become
 * "%XX" (upper-case hex), so the conversion is reversible and code-page
 * free (the package does not say which code page its names use, so a
 * double-byte character whose trail byte is 0x5C still splits the path
 * there; every known package has ASCII names only).  A name that repeats an
 * earlier one (the script's included, compared without ASCII case) gets
 * "%_<record index>" inserted before its extension; "%_" never occurs in a
 * converted name otherwise.  A name with an empty,
 * "." or ".." component, a component ending in '.' or ' ', a Windows device
 * name or one of : < > " | ? * is listed but refused on extraction.
 */
typedef struct xx_sfx_warpin_package {
    Abstractformat format;
    uint64_t number_of_records; /**< Script plus members. */
    uint32_t number_of_packages;
    uint32_t revision;
} xx_sfx_warpin_package;

typedef xx_sfx_warpin_package xx_sfx_warpin_package_t;

/** Name of the install-script record. */
#define XX_SFX_WARPIN_PACKAGE_SCRIPT_NAME "install.wis"

XXFC_API void xx_sfx_warpin_package_init(xx_sfx_warpin_package *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_sfx_warpin_package *xx_sfx_warpin_package_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_warpin_package_destroy(xx_sfx_warpin_package *archive);
XXFC_API void xx_sfx_warpin_package_free(xx_sfx_warpin_package *archive);

XXFC_API bool xx_sfx_warpin_package_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_sfx_warpin_package_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_warpin_package_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_warpin_package_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_warpin_package_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_warpin_package_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_warpin_package_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_warpin_package_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_warpin_package_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_WARPIN_PACKAGE_H */
