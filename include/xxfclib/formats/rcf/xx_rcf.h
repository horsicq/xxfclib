/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rcf.h @brief RCF installer archive reader. */

#ifndef XXFCLIB_FORMAT_RCF_H
#define XXFCLIB_FORMAT_RCF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RCF 1.0 installer archive: a 10 byte header, PKWARE DCL imploded member streams tiling the middle of the file, and a fixed-stride directory ending in the member count at EOF.
 */
typedef struct xx_rcf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rcf;

typedef xx_rcf xx_rcf_t;

XXFC_API void xx_rcf_init(xx_rcf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rcf *xx_rcf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rcf_destroy(xx_rcf *archive);
XXFC_API void xx_rcf_free(xx_rcf *archive);

XXFC_API bool xx_rcf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_rcf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rcf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rcf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rcf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rcf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rcf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RCF_H */
