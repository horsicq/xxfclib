/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_flashjester_jugglor.h
 *  @brief FlashJester Jugglor 2.x projector bundles (and "Exe Attachment"). */

#ifndef XXFCLIB_FORMAT_SFX_FLASHJESTER_JUGGLOR_H
#define XXFCLIB_FORMAT_SFX_FLASHJESTER_JUGGLOR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A FlashJester Jugglor 2.x executable and its attached files.
 *
 * Jugglor wraps a Flash projector, the "jesterrun.dll" engine and the
 * project's own files into one Delphi PE32 stub.  The files sit in the PE
 * overlay, i.e. right behind the raw data of the stub's last section.  Every
 * record there starts with the u32 0x6A4A61A3 and ends with the u32
 * 0x8CF9BF0D.  Strings are Delphi ShortStrings (a length byte and 255
 * bytes) in the ANSI code page.
 *
 * Jugglor 2.x member header (0x31C bytes), followed by its zlib stream:
 *
 *   +0x000  u32 LE  0x6A4A61A3
 *   +0x004  ShortString  file name (1..255 printable bytes)
 *   +0x104  ShortString  directory the file was taken from ("C:\...\")
 *   +0x204  ShortString  unused (empty)
 *   +0x304  u32 LE  unpacked size
 *   +0x308  u32 LE  size of the zlib stream (RFC 1950, Adler-32 included)
 *   +0x30C  u64 LE  last-write time, local wall clock in FILETIME units
 *   +0x314  u32     unknown (per-file value)
 *   +0x318  u32 LE  0x8CF9BF0D
 *
 * The member chain ends with a 220-byte trailer (the last bytes of the file):
 *
 *   +0x00  u32 LE  0x6A4A61A3
 *   +0x04  u32 LE  overlay offset (the first member header)
 *   +0x08  u32 LE  size of the member chain (overlay .. trailer)
 *   +0x0C  u32 LE  number of members
 *   +0x10  u32     zero
 *   +0x14  u32 LE  sum of the unpacked sizes
 *   +0x18  ShortString  "Jester Jugglor Version 2.52" (stale bytes behind)
 *   +0xD4  u32     unknown
 *   +0xD8  u32 LE  0x8CF9BF0D
 *
 * "Exe Attachment" (U3's "SFX ExeAttachment") uses the same record family
 * with a 0x220-byte header -- magic, name at +0x004, directory at +0x104,
 * unpacked size at +0x204, stream size at +0x208, FILETIME at +0x20C and
 * 0x8CF9BF0D at +0x21C -- and a 0x116-byte record (magic at +0, 0x8CF9BF0D
 * at +0x112) behind every stream.
 *
 * Members are published as "<directory without drive>/<name>" with '/'
 * separators, which is how U3 lays them out.
 */
typedef struct xx_sfx_flashjester_jugglor {
    Abstractformat format;
    uint32_t variant;           /**< XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_*. */
    uint64_t number_of_records;
    uint64_t unpacked_size;     /**< Sum of the members' unpacked sizes. */
    int64_t payload_offset;     /**< First member header, from the base. */
    int64_t payload_end;        /**< End of the trailer/last record. */
} xx_sfx_flashjester_jugglor;

typedef xx_sfx_flashjester_jugglor xx_sfx_flashjester_jugglor_t;

/** Jugglor 2.x: 0x31C-byte member headers and the 220-byte trailer. */
#define XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_JUGGLOR 1U
/** Exe Attachment: 0x220-byte headers, a 0x116-byte record after each. */
#define XX_SFX_FLASHJESTER_JUGGLOR_VARIANT_EXE_ATTACHMENT 2U

/** XX_META_ID_COMPRESSION_METHOD value of a member (zlib, RFC 1950). */
#define XX_SFX_FLASHJESTER_JUGGLOR_METHOD_ZLIB 8U

XXFC_API void xx_sfx_flashjester_jugglor_init(
    xx_sfx_flashjester_jugglor *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_flashjester_jugglor *xx_sfx_flashjester_jugglor_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_flashjester_jugglor_destroy(
    xx_sfx_flashjester_jugglor *archive);
XXFC_API void xx_sfx_flashjester_jugglor_free(
    xx_sfx_flashjester_jugglor *archive);

XXFC_API bool xx_sfx_flashjester_jugglor_check_is_valid(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_sfx_flashjester_jugglor_handle_base_info(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_flashjester_jugglor_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_flashjester_jugglor_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_flashjester_jugglor_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_flashjester_jugglor_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_flashjester_jugglor_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_flashjester_jugglor_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_flashjester_jugglor_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Inflate the current member into @p destination (NULL only verifies).
 *
 * Succeeds only when the stream yields exactly the declared unpacked size
 * and its Adler-32 matches.
 */
XXFC_API bool xx_sfx_flashjester_jugglor_unpack_current_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_FLASHJESTER_JUGGLOR_H */
