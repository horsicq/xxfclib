/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cri_awb.h @brief CRI AFS2 Wave Bank archive reader. */

#ifndef XXFCLIB_FORMAT_CRI_AWB_H
#define XXFCLIB_FORMAT_CRI_AWB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CRI AFS2 Wave Bank container. */
typedef struct xx_cri_awb {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_cri_awb;

typedef xx_cri_awb xx_cri_awb_t;

XXFC_API void xx_cri_awb_init(xx_cri_awb *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_cri_awb *xx_cri_awb_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_cri_awb_destroy(xx_cri_awb *archive);
XXFC_API void xx_cri_awb_free(xx_cri_awb *archive);

XXFC_API bool xx_cri_awb_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_cri_awb_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_cri_awb_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cri_awb_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cri_awb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cri_awb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cri_awb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cri_awb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cri_awb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CRI_AWB_H */
