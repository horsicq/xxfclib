/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ivt.h @brief MediaView title reader. */

#ifndef XXFCLIB_FORMAT_IVT_H
#define XXFCLIB_FORMAT_IVT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MediaView (IVT) title: a b-tree directory of internal file names, each naming a byte range that is either raw or an MSZIP stream under a twelve-byte header.
 */
typedef struct xx_ivt {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ivt;

typedef xx_ivt xx_ivt_t;

XXFC_API void xx_ivt_init(xx_ivt *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ivt *xx_ivt_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ivt_destroy(xx_ivt *archive);
XXFC_API void xx_ivt_free(xx_ivt *archive);

XXFC_API bool xx_ivt_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ivt_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ivt_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ivt_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ivt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ivt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ivt_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ivt_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ivt_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IVT_H */
