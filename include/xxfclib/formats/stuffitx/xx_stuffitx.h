/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_stuffitx.h @brief StuffIt X (.sitx) archive reader. */

#ifndef XXFCLIB_FORMAT_STUFFITX_H
#define XXFCLIB_FORMAT_STUFFITX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A StuffIt X archive (StuffIt 7 and later, ".sitx").
 *
 * The file is "StuffIt!" followed by one continuous stream of elements.
 * Everything is read as an LSB-first bit stream; a byte-level read (a data
 * block, a CRC, a skipped payload) always starts at the next byte boundary
 * and drops the unread bits of the current byte.  Integers are "P2" codes:
 * a unary count N (N-1 one bits and a zero), then bits least significant
 * first until N one bits have been seen; the value is that number minus 1.
 *
 * Element: 1 flag bit, P2 type, then (P2 key, P2 value) attribute pairs up
 * to key 0, then (P2 key, P2 value) algorithm pairs up to key 0 (key 4, the
 * cipher, carries one more P2).  Attribute keys used here: 1 object id,
 * 2 parent / entry, 3 stream, 4 fork index, 5 size.  Algorithm keys:
 * 1 compression, 2 checksum (0 = CRC-32), 3 preprocessing, 4 cipher.
 *
 *   0 end        2 file        4 directory   6 clue (attr 5 bytes follow)
 *   1 data       3 fork (+P2 fork type: 0 data, 1 resource)
 *   5 catalog    7 root (+P2)  8, 9 empty    >10 skipped with its blocks
 *
 * Data and catalog elements are followed, at the next byte boundary, by
 * blocks (P2 size, then that many bytes) up to a zero size, then a byte
 * boundary and trailing chunks of the same shape; a first chunk of 4 bytes
 * is the big-endian CRC-32 of the unpacked stream.  One data element holds
 * the forks of any number of entries back to back (fork index order).  The
 * catalog element carries names and metadata for every entry seen so far.
 *
 * Compression methods decoded: none (-1), the StuffIt X deflate variant (3:
 * one window byte that must be 15, then deflate with a 6-bit HDIST field)
 * and RC4 obscuring (5: two skipped bytes, a one-byte key).  The x86
 * preprocessing filter (2) is also decoded.  Brimstone/PPMd (0), Cyanide
 * (1), Darkhorse (2), Blend (4), Iron (6), other preprocessing filters and
 * ciphers are listed but not unpacked.  Base-N encoded archives
 * ("StuffIt?") are not claimed.
 */
typedef struct xx_stuffitx {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_size;
    bool truncated;     /**< Data ran out before the end element. */
    bool names_missing; /**< No catalog could be read; names are synthetic. */
} xx_stuffitx;

typedef xx_stuffitx xx_stuffitx_t;

XXFC_API void xx_stuffitx_init(xx_stuffitx *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_stuffitx *xx_stuffitx_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stuffitx_destroy(xx_stuffitx *archive);
XXFC_API void xx_stuffitx_free(xx_stuffitx *archive);

XXFC_API bool xx_stuffitx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_stuffitx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_stuffitx_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_stuffitx_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stuffitx_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stuffitx_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stuffitx_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stuffitx_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stuffitx_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STUFFITX_H */
