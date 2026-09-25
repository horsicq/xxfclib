/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sfx_ebook_compiler_executables.h
 * @brief eBook compiler executables: a viewer .exe with the book's pages
 *        appended as a private file store.
 *
 * Two builders are read, both found at the overlay (the end of the last
 * section's raw data) or at the offset their own trailer records.  Integers
 * are little-endian; offsets are relative to the overlay unless noted.
 *
 * eBook Creator (an MFC viewer; the store is an MFC CArchive stream of two
 * CUpdateDir lists of CUpdateElem objects):
 *
 *   +0x00  2   0
 *   +0x02  2   2, the number of lists
 *   +0x04  16  FFFF, schema 1, u16 10, "CUpdateDir"   (new-class record)
 *   +0x14  2   list id (0x015D, the files)
 *   +0x16  2   file count N
 *   +0x18  17  FFFF, schema 1, u16 11, "CUpdateElem"
 *   +0x29  2   element id 1
 *   +0x2B      first file: u32 block size, then the block: u32 unpacked
 *              size and a zlib stream (RFC 1950, Adler-32 trailer) of
 *              block size - 4 bytes
 *   then       each further file: u16 0x8003 (class reference), u16 id,
 *              u32 block size, block
 *
 *   The second list follows: u16 0x8001 (CUpdateDir reference), u16 list
 *   id (0x015E), u16 element count, and elements of the form u16 0x8003,
 *   u16 type, u32 size, data.  Type 2, the first, is the name table: per
 *   file a NUL-terminated name ('/' separators), u32 1-based file index
 *   and three u32 flags.  The others are viewer settings (title, author,
 *   splash bitmap, ...).  The book ends with u32 overlay offset and two
 *   letters ("KG" / "KH").  Without a usable name table the files are
 *   named by their 0-based index.
 *
 * SBook Builder (a Delphi viewer):
 *
 *   +0x00  4   5
 *   +0x04  5   "Sbook" or "Ebook"
 *   +0x09  4   unknown
 *   +0x0D      files, each: u32 (the file count on the first file, 0 on
 *              the others), u32 name length, name (the author's absolute
 *              Windows path), u32 DOS time and date, 4 bytes ("EC2\0"),
 *              then chunks: u32 packed size and a zlib stream.  Every
 *              chunk but the last unpacks to exactly 0x4000 bytes.
 *   end        u32 0, u32 0, then u32 overlay offset (from the start of
 *              the executable) as the last four bytes of the file
 *
 *   A drive ("C:") and the leading separators are removed from the stored
 *   path, so "c:\my documents\book\home.htm" is listed as
 *   "my documents/book/home.htm".
 *
 * The executable is parsed only as far as its section table; no code in it
 * is run.  Names that stay unsafe after that (a ".." component, control
 * characters, device names, ...) are listed but never written.
 */

#ifndef XXFCLIB_FORMAT_SFX_EBOOK_COMPILER_EXECUTABLES_H
#define XXFCLIB_FORMAT_SFX_EBOOK_COMPILER_EXECUTABLES_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Builders this reader recognises. */
#define XX_SFX_EBOOK_VARIANT_NONE 0U
#define XX_SFX_EBOOK_VARIANT_EBOOK_CREATOR 1U
#define XX_SFX_EBOOK_VARIANT_SBOOK_BUILDER 2U

typedef struct xx_sfx_ebook_compiler_executables {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t variant;           /**< XX_SFX_EBOOK_VARIANT_* */
    int64_t payload_offset;     /**< Start of the store, relative to the base. */
    int64_t payload_end;        /**< End of the store, relative to the base. */
    int64_t names_offset;       /**< eBook Creator name table, or -1. */
    uint32_t header_value;      /**< The unknown header field (see above). */
    bool has_names;             /**< eBook Creator: the name table is usable. */
    bool has_trailer;           /**< The trailer points back at the store. */
    char tag[6];                /**< SBook Builder: "Sbook" or "Ebook". */
} xx_sfx_ebook_compiler_executables;

typedef xx_sfx_ebook_compiler_executables xx_sfx_ebook_compiler_executables_t;

XXFC_API void xx_sfx_ebook_compiler_executables_init(
    xx_sfx_ebook_compiler_executables *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_ebook_compiler_executables *
xx_sfx_ebook_compiler_executables_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_sfx_ebook_compiler_executables_destroy(
    xx_sfx_ebook_compiler_executables *archive);
XXFC_API void xx_sfx_ebook_compiler_executables_free(
    xx_sfx_ebook_compiler_executables *archive);

XXFC_API bool xx_sfx_ebook_compiler_executables_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfx_ebook_compiler_executables_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_ebook_compiler_executables_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_ebook_compiler_executables_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_ebook_compiler_executables_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_ebook_compiler_executables_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_ebook_compiler_executables_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_ebook_compiler_executables_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_ebook_compiler_executables_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_EBOOK_COMPILER_EXECUTABLES_H */
