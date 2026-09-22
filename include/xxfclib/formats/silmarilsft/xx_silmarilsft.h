/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_silmarilsft.h @brief Silmarils game-resource container reader. */

#ifndef XXFCLIB_FORMAT_SILMARILSFT_H
#define XXFCLIB_FORMAT_SILMARILSFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Silmarils game-resource container (.IO / .CO / .DO).
 *
 * One header plus one packed stream: no directory, no member name, no
 * checksum.
 *
 *   0x00  u32  method << 24 | raw size      ENDIAN DEPENDENT
 *   0x04  u16  version, always 1
 *   0x06  u8[8] code table, method 0xa1 only, always
 *                0b 09 0a 0b 07 05 06 07
 *
 * THE FILE HAS NO MAGIC.  The version word is the only constant field and it
 * is what pins the byte order: the PC build writes every scalar little
 * endian, the Amiga/ST build big endian, so `01 00` at 0x04 means little and
 * `00 01` means big.  The packed stream itself is a byte stream and is
 * identical in both builds.
 *
 * The 24-bit raw size counts the six-byte header, so the plaintext is
 * `raw size - 6`.
 *
 * Two methods exist:
 *   0x81  byte-run codec - implemented
 *   0xa1  bit-stream LZ codec - NOT implemented.  Its header, byte order and
 *         constant parameter block are understood but its match token is not,
 *         so those members are listed and extraction refuses rather than
 *         writing plausible-looking garbage.
 *
 * Detection is earned rather than assumed: a 0x81 stream is trial-walked in
 * full and has to produce exactly `raw size - 6` bytes and stop exactly on
 * the last input byte; a 0xa1 stream has to carry the constant parameter
 * block.  Ported from XArchive's games/xsilmarils.cpp and
 * Algos/xsilmarilsdecoder.cpp.
 */
typedef struct xx_silmarilsft {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;
    uint32_t method;      /**< 0x81 byte-run, 0xa1 bit-stream. */
    bool big_endian;
    bool method_supported;
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
