/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_livemaker.h @brief LiveMaker "vf" resource archive reader. */

#ifndef XXFCLIB_FORMAT_LIVEMAKER_H
#define XXFCLIB_FORMAT_LIVEMAKER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LiveMaker resource archive (game.dat, or appended to the game EXE).
 * All fields little-endian; every offset is relative to the "vf" header.
 *
 *   0x00  2  "vf"
 *   0x02  4  version (0x66, so the file starts "vff\0")
 *   0x06  4  entry count
 *   0x0A     count names: u32 length (1..256) + cp932 bytes, each byte XOR
 *            the low byte of the next TpRandom(0x75D6EE39) value; the
 *            keystream runs on across all names
 *            count+1 u64 offsets, each XOR the sign-extended next value of
 *            a freshly reset TpRandom; entry i spans [off[i], off[i+1])
 *            count u8 flags: 0 zlib, 1 stored, 2 scrambled, 3 scrambled
 *            then zlib
 *            (further per-entry tables follow; they are not needed)
 *
 * A scrambled entry starts with u32 chunk size and u32 seed (XOR 0xF8EA);
 * its chunks are permuted by a seeded generator and put back in order here.
 *
 * Archives whose index lives in a separate .ext file, and extra .001 ...
 * parts, are not handled: the reader opens a self-contained archive at
 * base_address.  An archive appended to the game EXE is found by the format
 * search through the "vff\0" anchor.
 */
typedef struct xx_livemaker {
    Abstractformat format;
    uint64_t number_of_records;
} xx_livemaker;

typedef xx_livemaker xx_livemaker_t;

XXFC_API void xx_livemaker_init(xx_livemaker *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_livemaker *xx_livemaker_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_livemaker_destroy(xx_livemaker *archive);
XXFC_API void xx_livemaker_free(xx_livemaker *archive);

XXFC_API bool xx_livemaker_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_livemaker_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_livemaker_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_livemaker_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_livemaker_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_livemaker_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_livemaker_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_livemaker_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_livemaker_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LIVEMAKER_H */
