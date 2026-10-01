/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ddd.h @brief DDD / DDD Pro (Dalton's Disk Disintegrator) reader. */

#ifndef XXFCLIB_FORMAT_DDD_H
#define XXFCLIB_FORMAT_DDD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A 5.25" Apple II disk compressed by Dalton's Disk Disintegrator.
 *
 * Layout (DDD 2.x / DDD Pro, as unpacked by CiderPress):
 *   0x00  4 bytes that carry no information (the DOS 3.3 load address and
 *         length of a 'B' file, or DDD Pro's constant 03 c9 bf d0)
 *   0x04  a bit stream, read most-significant bit first:
 *         3 zero bits, the 8-bit disk volume number, then 35 tracks.  Every
 *         track restores exactly 4096 bytes (16 DOS-ordered sectors) and
 *         starts with a table of 20 "favourite" bytes, 8 bits each.  The
 *         track body is a sequence of codes:
 *           0 + 8 bits              literal byte
 *           1 + 3..6 bits           one of the 20 favourites (prefix code)
 *           10010111 + 8 + 8 bits   run: byte, count (0 = 256)
 *         Every 8-bit field is stored least-significant bit first.
 *   After the last track at most 256 bytes of padding follow (the rest of a
 *   DOS sector, plus a zero byte DDD Pro writes).
 *
 * There is no magic.  The format is recognised the way CiderPress does it:
 * the whole stream has to decode to exactly 35 x 4096 bytes, no run may cross
 * a track boundary, the input must not run out, and no more than 256 bytes
 * may remain afterwards.  The detection probe (check_is_valid) additionally
 * requires that no literal repeats a byte from its track's favourite table -
 * an encoder always spends the shorter favourite code on such a byte - which
 * random data fails within the first few hundred codes.
 *
 * The single member, image.do, is the 143,360-byte DOS-ordered disk image.
 */
typedef struct xx_ddd {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t volume_number;
    int64_t stream_size; /**< Bytes consumed by the bit stream, incl. 4-byte lead. */
} xx_ddd;

typedef xx_ddd xx_ddd_t;

XXFC_API void xx_ddd_init(xx_ddd *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_ddd *xx_ddd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ddd_destroy(xx_ddd *archive);
XXFC_API void xx_ddd_free(xx_ddd *archive);

XXFC_API bool xx_ddd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ddd_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ddd_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_ddd_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ddd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ddd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ddd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ddd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ddd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DDD_H */
