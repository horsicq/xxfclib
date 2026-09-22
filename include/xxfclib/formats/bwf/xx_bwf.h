/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bwf.h @brief Beame & Whiteside distribution file reader. */

#ifndef XXFCLIB_FORMAT_BWF_H
#define XXFCLIB_FORMAT_BWF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Beame & Whiteside BW-Connect distribution file: a magic-less chain of 22-byte records, each immediately followed by its PKWARE DCL imploded payload, tiling the file exactly to its end.
 */
typedef struct xx_bwf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bwf;

typedef xx_bwf xx_bwf_t;

XXFC_API void xx_bwf_init(xx_bwf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_bwf *xx_bwf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_bwf_destroy(xx_bwf *archive);
XXFC_API void xx_bwf_free(xx_bwf *archive);

XXFC_API bool xx_bwf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_bwf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_bwf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_bwf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bwf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bwf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bwf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bwf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bwf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BWF_H */
