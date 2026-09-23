/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_silmarilsft.h @brief Silmarils ALIS script container reader. */

#ifndef XXFCLIB_FORMAT_SILMARILSFT_H
#define XXFCLIB_FORMAT_SILMARILSFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Silmarils ALIS script container (.IO / .CO / .DO).
 *
 * One header plus one packed stream: no directory, no member name, no
 * checksum.
 *
 *   0x00  u32  method << 24 | raw size      ENDIAN DEPENDENT
 *   0x04  u16  "not main script" flag, always 1 in a resource file
 *   0x06  u8[8] offset-width table, method 0xa1 only, always
 *                0b 09 0a 0b 07 05 06 07
 *
 * THE FILE HAS NO MAGIC.  The flag word is the only constant field and it
 * is what pins the byte order: the PC build writes every scalar little
 * endian, the Amiga/ST build big endian, so `01 00` at 0x04 means little and
 * `00 01` means big.  The packed stream itself is identical in both builds.
 * (A main script has 0 there plus 16 bytes of VM specs; it is not claimed.)
 *
 * The 24-bit raw size counts the six-byte header, so the plaintext is
 * `raw size - 6`.
 *
 * Two methods are decoded:
 *   0x81  byte-run codec: c < 0x80 copies c literals, c >= 0x80 repeats the
 *         next byte (c & 0x7f) times.
 *   0xa1  bit-stream LZ codec: MSB-first bits from big-endian 16-bit words;
 *         a flag bit for an optional literal run, then a 3-bit selector
 *         choosing the offset width from the table and the match length.
 * (0x80, the interleaved byte-run variant, does not occur in the reference
 * corpus and is not claimed.)
 *
 * Detection is earned rather than assumed: a 0x81 stream is trial-walked in
 * full and has to produce exactly `raw size - 6` bytes and stop exactly on
 * the last input byte; a 0xa1 stream has to carry the constant parameter
 * block and a plausible packed/unpacked ratio, and its decode has to end
 * within four bytes of the stream end.
 */
typedef struct xx_silmarilsft {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;
    uint32_t method;      /**< 0x81 byte-run, 0xa1 bit-stream. */
    bool big_endian;
    bool method_supported; /**< Both claimed methods decode; kept for API. */
} xx_silmarilsft;

typedef xx_silmarilsft xx_silmarilsft_t;

XXFC_API void xx_silmarilsft_init(xx_silmarilsft *archive,
                                  xx_io_device *device, int64_t base_address);
XXFC_API xx_silmarilsft *xx_silmarilsft_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_silmarilsft_destroy(xx_silmarilsft *archive);
XXFC_API void xx_silmarilsft_free(xx_silmarilsft *archive);

XXFC_API bool xx_silmarilsft_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_silmarilsft_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_silmarilsft_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_silmarilsft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_silmarilsft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_silmarilsft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_silmarilsft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_silmarilsft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_silmarilsft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SILMARILSFT_H */
