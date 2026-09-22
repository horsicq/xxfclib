/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_btoa.h @brief btoa / xbtoa ASCII transport reader. */

#ifndef XXFCLIB_FORMAT_BTOA_H
#define XXFCLIB_FORMAT_BTOA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A btoa ("binary to ASCII") stream, as written by xbtoa 4.x / 5.x.
 *
 *   a line "xbtoa Begin"
 *   the body: base-85 text, five characters '!'..'u' per four binary bytes,
 *     most significant base-85 digit first.  Two standalone shorthands may
 *     appear where a group would start: 'z' for four zero bytes and 'y' for
 *     four spaces.  Line breaks carry no meaning.
 *   a line "xbtoa End N <length> <length in hex> E <eor> S <sum> R <rot>"
 *
 * The trailer is what makes this worth decoding: `N` is the stored plaintext
 * length and the other three are checksums over the ZERO-PADDED output - the
 * body always codes a whole number of four-byte groups, and `N` says how much
 * of the last one is real, but the checksums were taken before that trim.
 *   E  eight-bit XOR of every byte
 *   S  sum of (byte + 1) over every byte, 32-bit
 *   R  rotate the 32-bit accumulator left by one, then add the byte
 *
 * All four are verified before a decode is handed back, so a stream that
 * decodes wrong is refused rather than returned.  In particular the "b2a"
 * dialect that adds a 'w' shorthand is recognised as btoa and listed, but its
 * expansion has never been pinned down, so unpack refuses for it instead of
 * inventing one.
 */
typedef struct xx_btoa {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< The trailer's N. */
    uint32_t stored_eor;
    uint32_t stored_sum;
    uint32_t stored_rot;
} xx_btoa;

typedef xx_btoa xx_btoa_t;

XXFC_API void xx_btoa_init(xx_btoa *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_btoa *xx_btoa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_btoa_destroy(xx_btoa *archive);
XXFC_API void xx_btoa_free(xx_btoa *archive);

XXFC_API bool xx_btoa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_btoa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_btoa_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_btoa_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_btoa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_btoa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_btoa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_btoa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_btoa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BTOA_H */
