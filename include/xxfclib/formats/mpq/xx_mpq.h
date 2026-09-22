/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mpq.h @brief Blizzard MoPaQ (MPQ) game archive. */

#ifndef XXFCLIB_FORMAT_MPQ_H
#define XXFCLIB_FORMAT_MPQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Blizzard MPQ: an encrypted hash table names block-table entries,
 * and the block table carries each member's extent and flags. */
typedef struct xx_mpq {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_mpq;

typedef struct xx_mpq xx_mpq_t;

XXFC_API void xx_mpq_init(xx_mpq *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_mpq *xx_mpq_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mpq_destroy(xx_mpq *archive);
XXFC_API void xx_mpq_free(xx_mpq *archive);
XXFC_API bool xx_mpq_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mpq_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mpq_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mpq_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mpq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mpq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mpq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mpq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mpq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_mpq_to_format(xx_mpq *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MPQ_H */
