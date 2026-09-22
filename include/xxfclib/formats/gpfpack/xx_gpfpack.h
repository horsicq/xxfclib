/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gpfpack.h @brief GPFPACK packed-file reader. */

#ifndef XXFCLIB_FORMAT_GPFPACK_H
#define XXFCLIB_FORMAT_GPFPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A GPFPACK packed file.
 *
 * One container holds exactly one member.  The 28-byte header carries the
 * literal "GPFPACK", a version word of 1 and a fixed 14-byte name field; the
 * payload that follows is a chain of blocks, each introduced by its own
 * 32-bit bit count, and the chain ends exactly at the end of the file.
 *
 * Each block is an independent LZW run - MSB-first, 9 to 13 bits, early
 * width change, CLEAR 0x100, first assignable code 0x102, and no end code at
 * all: a block ends when its declared bit budget is spent.
 *
 * No plaintext length is recorded, so the member's uncompressed size is
 * measured by running the chain once when the records are created.
 */
typedef struct xx_gpfpack {
    Abstractformat format;
    uint64_t number_of_records;
    /** Number of packed blocks in the payload chain. */
    uint64_t number_of_blocks;
    /** Version word at offset 0x0C (1 throughout the corpus). */
    uint16_t version;
} xx_gpfpack;

typedef xx_gpfpack xx_gpfpack_t;

XXFC_API void xx_gpfpack_init(xx_gpfpack *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_gpfpack *xx_gpfpack_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_gpfpack_destroy(xx_gpfpack *archive);
XXFC_API void xx_gpfpack_free(xx_gpfpack *archive);

XXFC_API bool xx_gpfpack_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_gpfpack_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_gpfpack_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_gpfpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gpfpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gpfpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gpfpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gpfpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gpfpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GPFPACK_H */
