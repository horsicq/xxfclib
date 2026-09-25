/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_o_setup.h @brief O'Setup95 / O'Setup for Windows installer. */

#ifndef XXFCLIB_FORMAT_O_SETUP_H
#define XXFCLIB_FORMAT_O_SETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A self-installing O'Setup package (Celtech Software, 1995-96): a
 * 16-bit (NE) or 32-bit (PE) setup stub with the package appended.
 *
 * Little endian throughout.  Offsets are from the start of the executable
 * (the reader's base address), which opens with "MZ".
 *
 * The package ends the file with a trailer, in one of two forms:
 *
 *   "OSETUPS\0" or "OSETUPA\0"  u32 first-record offset  u16 record count
 *   "OSETUP"                    u32 first-record offset  u16 record count
 *
 * (14 and 12 bytes; the 6-byte form comes from an older builder.)  The
 * records start at the first-record offset - the end of the stub image - and
 * follow one another directly; the last one ends exactly where the trailer
 * begins.  Each record is a 44-byte header and the member's data:
 *
 *   +0x00  "FILE"
 *   +0x04  u32   stored size of the data that follows the header
 *   +0x08  u32   last write time (Unix time)
 *   +0x0C  char[13] 8.3 name, NUL terminated; bytes after the NUL are
 *                uninitialised writer memory
 *   +0x19  19 bytes of uninitialised writer memory (not interpreted)
 *
 * A member's data is either the file itself or the file compressed by
 * Microsoft COMPRESS.EXE: an SZDD stream ("SZDD" 88 F0 27 33, mode 'A',
 * the missing name character, the u32 unpacked size, LZSS data).  There is
 * no flag for it: the setup engine expands a member through LZExpand, which
 * decompresses what opens with the SZDD header and copies anything else, and
 * the reader does the same.  The name in the record is always the installed
 * name, never the COMPRESS "_" form.  The first members are the engine
 * (_OSETUP.EXE or ~OSETUP.EXE), _OSETUP.LIC and _OSETUP.INF.
 *
 * Records: every member in file order.  XX_META_ID_COMPRESSION_METHOD is
 * XX_O_SETUP_METHOD_STORED or XX_O_SETUP_METHOD_SZDD, XX_META_ID_TIMESTAMP
 * the Unix last write time, and the uncompressed size the SZDD header's.
 *
 * Names: bytes outside 0x21..0x7E and any of % / \ : * ? " < > | become
 * "%XX" (upper-case hex), so a name is always one path component and the
 * conversion is reversible and code-page free.  A name that repeats an
 * earlier one (compared without ASCII case) gets "%_<record index>"
 * inserted before its extension; "%_" never occurs in a converted name
 * otherwise.  An empty name, ".", "..", a name ending in '.' or ' ' and a
 * Windows device name are listed but refused on extraction.
 */
typedef struct xx_o_setup {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t first_record;   /**< Absolute offset of the first "FILE" record. */
    int64_t trailer_offset; /**< Absolute offset of the trailer. */
    uint32_t variant;       /**< One of XX_O_SETUP_VARIANT_*. */
} xx_o_setup;

typedef xx_o_setup xx_o_setup_t;

#define XX_O_SETUP_VARIANT_OLD 0U     /**< 12-byte "OSETUP" trailer. */
#define XX_O_SETUP_VARIANT_SETUP 1U   /**< "OSETUPS\0": setup-only package. */
#define XX_O_SETUP_VARIANT_ALL 2U     /**< "OSETUPA\0": all files. */

#define XX_O_SETUP_METHOD_STORED 0U
#define XX_O_SETUP_METHOD_SZDD 1U

XXFC_API void xx_o_setup_init(xx_o_setup *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_o_setup *xx_o_setup_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_o_setup_destroy(xx_o_setup *archive);
XXFC_API void xx_o_setup_free(xx_o_setup *archive);

XXFC_API bool xx_o_setup_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_o_setup_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_o_setup_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_o_setup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_o_setup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_o_setup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_o_setup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_o_setup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_o_setup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_O_SETUP_H */
