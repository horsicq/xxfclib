/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sfx_krzip.h
 * @brief KRZIP setups (Kryloff Technologies, Delphi installer stub).
 *
 * A KRZIP setup is a Delphi PE stub with the package appended at the exact
 * end of the last section's raw data (the overlay).  Offsets below are
 * relative to that overlay; integers are little-endian.
 *
 *   +0x00  24     "<KRZIP FILE BEGINS HERE>" (plain)
 *   +0x18  ...    record stream, obfuscated (see below)
 *   end-4  4      ~CRC-32 of the de-obfuscated record stream (plain)
 *
 * The record stream runs from +0x18 to four bytes before the end of the
 * file.  Every byte of it is XORed with a keystream that is the Borland
 * Delphi Random(256) sequence from RandSeed 0: for the n-th byte (n = 1, 2,
 * ...) the state becomes state * 0x08088405 + 1 and the key is state >> 24.
 * The first key byte is therefore 0x00 and the second 0x08, which is why
 * the first name length reads as (raw u16 ^ 0x0800).
 *
 * Records follow each other until fewer than five bytes remain:
 *
 *   u16    name length, 1..0x400
 *   name   ANSI, '\' separated, not terminated
 *   u16    DOS time
 *   u16    DOS date
 *   u32    unpacked size
 *   u32    Windows attributes
 *   u32    packed size (the data that follows)
 *   u32    ~CRC-32 of the unpacked data
 *   data   packed size bytes
 *
 * Data is one of two encodings, told apart the way U3 does it -- byte 13
 * of the data is 'x' (the first byte of a zlib header):
 *
 *   PKWARE DCL implode (older builds), decoding to the unpacked size;
 *
 *   zlib: u8 1, u32 inflated size, u32 zlib stream size, u32 ~CRC-32 of the
 *   zlib stream, then the RFC 1950 stream.  It inflates to a length prefix
 *   (one byte 0x20 + n - 1, then an n-byte little-endian length, n = 1..4),
 *   that many bytes of member data, and a five-byte trailer (0x00 and the
 *   member's stored check), which is dropped.
 *
 * Records: every member in stream order; XX_META_ID_COMPRESSION_METHOD is
 * XX_SFX_KRZIP_METHOD_DCL or XX_SFX_KRZIP_METHOD_ZLIB, XX_META_ID_CRC32 the
 * member's CRC-32, and XX_META_ID_ATTRIBUTES, XX_META_ID_LAST_MOD_DATE and
 * XX_META_ID_LAST_MOD_TIME the stored fields.  Names are converted from
 * Windows-1252 to UTF-8 with '/' separators.  A later member whose name
 * repeats an earlier one (compared without case) gets "_2", "_3", ... in
 * front of its extension.  A name that is not a safe relative path is
 * listed but refused on extraction.  A member is written only after it
 * decoded completely and its CRC-32 matched.  Without
 * XX_META_ID_OPT_OVERWRITE an existing file is never replaced.
 *
 * The stub is parsed only as far as its section table; nothing in it is
 * executed.
 */

#ifndef XXFCLIB_FORMAT_SFX_KRZIP_H
#define XXFCLIB_FORMAT_SFX_KRZIP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_SFX_KRZIP_METHOD_DCL 1U
#define XX_SFX_KRZIP_METHOD_ZLIB 2U

typedef struct xx_sfx_krzip {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t overlay_offset; /**< The marker, relative to the base. */
    int64_t stream_offset;  /**< First record (overlay + 24). */
    int64_t stream_size;    /**< Record bytes, without the final check. */
    uint32_t stream_check;  /**< The stored u32 after the last record. */
} xx_sfx_krzip;

typedef xx_sfx_krzip xx_sfx_krzip_t;

XXFC_API void xx_sfx_krzip_init(xx_sfx_krzip *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_sfx_krzip *xx_sfx_krzip_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_sfx_krzip_destroy(xx_sfx_krzip *archive);
XXFC_API void xx_sfx_krzip_free(xx_sfx_krzip *archive);

XXFC_API bool xx_sfx_krzip_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_sfx_krzip_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_krzip_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_krzip_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sfx_krzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfx_krzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_krzip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_krzip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_krzip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_KRZIP_H */
