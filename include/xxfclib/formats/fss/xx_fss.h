/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fss.h @brief SSBOB slideshow container reader. */

#ifndef XXFCLIB_FORMAT_FSS_H
#define XXFCLIB_FORMAT_FSS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SSBOB slideshow container reader.
 */
typedef struct xx_fss {
    Abstractformat format;
    uint64_t number_of_records;
} xx_fss;

typedef xx_fss xx_fss_t;

XXFC_API void xx_fss_init(xx_fss *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_fss *xx_fss_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_fss_destroy(xx_fss *archive);
XXFC_API void xx_fss_free(xx_fss *archive);

XXFC_API bool xx_fss_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_fss_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_fss_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_fss_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fss_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fss_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fss_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fss_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fss_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FSS_H */
