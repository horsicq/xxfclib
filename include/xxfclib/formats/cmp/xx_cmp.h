/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cmp.h @brief CMP archive reader. */

#ifndef XXFCLIB_FORMAT_CMP_H
#define XXFCLIB_FORMAT_CMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single-member .CMP container: a 61-byte header carrying the original name and the plaintext length, followed by one compressed stream running to end-of-file.
 */
typedef struct xx_cmp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cmp;

typedef xx_cmp xx_cmp_t;

XXFC_API void xx_cmp_init(xx_cmp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_cmp *xx_cmp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_cmp_destroy(xx_cmp *archive);
XXFC_API void xx_cmp_free(xx_cmp *archive);

XXFC_API bool xx_cmp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_cmp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_cmp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cmp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cmp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cmp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cmp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cmp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cmp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CMP_H */
