/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_stac.h
 *  @brief Stac Electronics LZS single-stream container ("sTaC").
 */

/* WHERE THE LAYOUT COMES FROM.  U3's recognition predicate for its "STAC"
 * handler (FUN_005d0a00, reached from VMT slot 0 at 0x005d0c80) is a single
 * test: the little-endian u32 at offset 0 equals 0x43615473, i.e. the four
 * ASCII bytes "sTaC".  There is nothing else in the header - no length, no
 * name, no checksum - and the LZS bit stream starts immediately at offset 4.
 *
 *   off  size  what
 *   0    4     "sTaC"
 *   4    ..    LZS bit stream, ending on the LZS end marker
 *
 * The payload codec is Stac Electronics LZS, the same scheme later published
 * as ANSI X3.241-1994 and as RFC 2395 for PPP.  It is a plain LZ77 over a
 * 2048-byte window with no entropy coder:
 *
 *   0 + 8 bits              literal byte
 *   1 1 + 7 bits            match offset 1..127
 *   1 0 + 11 bits           match offset 128..2047
 *   1 1 + 0000000           END MARKER
 *   length after an offset: 00->2 01->3 10->4
 *                           1100->5 1101->6 1110->7
 *                           1111 nnnn -> 8+nnnn for nnnn<15
 *                           1111 1111 nnnn -> 23+nnnn, repeating in
 *                                             steps of 15 while nnnn==15
 *
 * Bits are consumed most significant first within each byte.
 *
 * THIS IS VERIFIED, NOT GUESSED.  All twelve samples in F:\ARC\ARC\STAC decode
 * with that grammar, each one reaching the end marker on the LAST byte of the
 * file - the decoder consumes 357/357, 1044/1044, 4811/4811, 1466/1466,
 * 1434/1434, 1879/1879, 28574/28574, 2687/2687, 11478/11478, 7507/7507,
 * 23540/23540 and 26151/26151 bytes respectively - and the decoded output
 * begins with a recognisable header every time ("MZ" for the five .EXE
 * members, "MULTIWARE KE..." for US.KB, an 8086 jump for the .COM members).
 * A wrong grammar cannot land on the end marker exactly at EOF twelve times
 * out of twelve, so the codec is settled.
 *
 * WHAT THIS READER DOES.  It publishes ONE archive record covering the whole
 * decoded stream and decodes it for real.  The decoded length is not stored
 * anywhere in the file, so it is measured by a first, output-free pass of the
 * same decoder that then produces the bytes; the two can therefore never
 * disagree.  The member has no name in the container - U3 falls back to the
 * archive's own file name for it - so this reader publishes the neutral name
 * "stac.bin" rather than inventing one from the host file.
 */

#ifndef XXFCLIB_FORMAT_STAC_H
#define XXFCLIB_FORMAT_STAC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Container magic, four bytes at offset 0. */
#define XX_STAC_SIGNATURE "sTaC"
/** Bytes compared by the magic test. */
#define XX_STAC_SIGNATURE_SIZE 4U
/** Header size; the LZS stream starts here. */
#define XX_STAC_HEADER_SIZE 4U
/** Hard ceiling on a decoded member, so a short file cannot ask for a huge
 *  allocation through a long chain of matches. */
#define XX_STAC_MAX_DECODED_SIZE ((int64_t)256 * 1024 * 1024)

typedef struct xx_stac xx_stac;
typedef struct xx_stac xx_stac_t;
typedef struct xx_stac XStac;

struct xx_stac {
    Abstractformat format;  /**< Base format structure (first member). */
    int64_t packed_offset;  /**< Absolute offset of the LZS stream. */
    int64_t packed_size;    /**< LZS bytes actually consumed by the decode. */
    int64_t unpacked_size;  /**< Measured decoded length. */
};

XXFC_API void xx_stac_init(xx_stac *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_stac *xx_stac_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stac_destroy(xx_stac *archive);
XXFC_API void xx_stac_free(xx_stac *archive);

XXFC_API bool xx_stac_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_stac_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_stac_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_stac_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_stac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stac_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stac_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stac_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the LZS stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_stac_get_packed_offset(const xx_stac *archive);
/** LZS bytes consumed, or -1 before handle_base_info. */
XXFC_API int64_t xx_stac_get_packed_size(const xx_stac *archive);
/** Decoded length, or -1 before handle_base_info. */
XXFC_API int64_t xx_stac_get_unpacked_size(const xx_stac *archive);

static inline Abstractformat *xx_stac_to_format(xx_stac *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STAC_H */
