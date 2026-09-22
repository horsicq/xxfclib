/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zfsf.h @brief ZFSF archive reader. */

#ifndef XXFCLIB_FORMAT_ZFSF_H
#define XXFCLIB_FORMAT_ZFSF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZFSF archive. Its directory is a linked chain of fixed-capacity groups rather than one flat table, so the walk follows links and stops on the declared total.
 */
typedef struct xx_zfsf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zfsf;

typedef xx_zfsf xx_zfsf_t;

XXFC_API void xx_zfsf_init(xx_zfsf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zfsf *xx_zfsf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zfsf_destroy(xx_zfsf *archive);
XXFC_API void xx_zfsf_free(xx_zfsf *archive);

XXFC_API bool xx_zfsf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zfsf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zfsf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zfsf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zfsf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zfsf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zfsf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zfsf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zfsf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZFSF_H */
