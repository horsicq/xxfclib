/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lha.h @brief LHA/LZH archive reader. */

#ifndef XXFCLIB_FORMAT_LHA_H
#define XXFCLIB_FORMAT_LHA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An LHA/LZH archive: a chain of self-sized member headers, each immediately followed by its payload, ending at EOF or at a zero header-size byte.
 */
typedef struct xx_lha {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lha;

typedef xx_lha xx_lha_t;

XXFC_API void xx_lha_init(xx_lha *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lha *xx_lha_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lha_destroy(xx_lha *archive);
XXFC_API void xx_lha_free(xx_lha *archive);

XXFC_API bool xx_lha_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lha_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lha_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lha_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lha_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lha_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lha_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lha_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lha_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LHA_H */
