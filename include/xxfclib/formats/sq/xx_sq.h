/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sq.h @brief SQ (0x53 0x51 0xAC 0xAE) single-file squeezed container. */

#ifndef XXFCLIB_FORMAT_SQ_H
#define XXFCLIB_FORMAT_SQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The SQ container: one squeezed file carrying its original DOS name and
 * a six-byte calendar stamp, coded with adaptive Huffman over 32 KiB
 * LZ77 matches. */
typedef struct xx_sq {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_sq;

typedef struct xx_sq xx_sq_t;

XXFC_API void xx_sq_init(xx_sq *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_sq *xx_sq_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sq_destroy(xx_sq *archive);
XXFC_API void xx_sq_free(xx_sq *archive);
XXFC_API bool xx_sq_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sq_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sq_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_sq_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_sq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_sq_to_format(xx_sq *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQ_H */
