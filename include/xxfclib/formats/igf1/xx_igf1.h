/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_igf1.h @brief IGF compressed file reader. */

#ifndef XXFCLIB_FORMAT_IGF1_H
#define XXFCLIB_FORMAT_IGF1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IGF compressed file: a single 0x38-byte header, the member name behind it, and one LHA -lh4- stream at the offset the header states.
 */
typedef struct xx_igf1 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_igf1;

typedef xx_igf1 xx_igf1_t;

XXFC_API void xx_igf1_init(xx_igf1 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_igf1 *xx_igf1_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_igf1_destroy(xx_igf1 *archive);
XXFC_API void xx_igf1_free(xx_igf1 *archive);

XXFC_API bool xx_igf1_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_igf1_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_igf1_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_igf1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_igf1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_igf1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_igf1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_igf1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_igf1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IGF1_H */
