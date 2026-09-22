/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_tgcf.h @brief TGCF archive reader. */

#ifndef XXFCLIB_FORMAT_TGCF_H
#define XXFCLIB_FORMAT_TGCF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TGCF archive: a versioned volume header followed by a chain of self-signed member records, each carrying a short and a long Windows path and either stored or zlib-compressed bytes.
 */
typedef struct xx_tgcf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tgcf;

typedef xx_tgcf xx_tgcf_t;

XXFC_API void xx_tgcf_init(xx_tgcf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_tgcf *xx_tgcf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_tgcf_destroy(xx_tgcf *archive);
XXFC_API void xx_tgcf_free(xx_tgcf *archive);

XXFC_API bool xx_tgcf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_tgcf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_tgcf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_tgcf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tgcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tgcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tgcf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tgcf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tgcf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TGCF_H */
