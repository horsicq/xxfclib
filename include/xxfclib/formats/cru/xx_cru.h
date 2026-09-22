/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cru.h @brief CRUSH archive reader. */

#ifndef XXFCLIB_FORMAT_CRU_H
#define XXFCLIB_FORMAT_CRU_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CRUSH archive: a 26-byte banner pointing at a table of fixed 24-byte entries, followed by the directory name pool.
 */
typedef struct xx_cru {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cru;

typedef xx_cru xx_cru_t;

XXFC_API void xx_cru_init(xx_cru *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_cru *xx_cru_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_cru_destroy(xx_cru *archive);
XXFC_API void xx_cru_free(xx_cru *archive);

XXFC_API bool xx_cru_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_cru_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_cru_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cru_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cru_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cru_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cru_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cru_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cru_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CRU_H */
