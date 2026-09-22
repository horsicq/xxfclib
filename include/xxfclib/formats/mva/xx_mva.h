/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mva.h @brief MVA installer archive reader. */

#ifndef XXFCLIB_FORMAT_MVA_H
#define XXFCLIB_FORMAT_MVA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An MVA installer archive: an 8-byte container header followed by a chain of fixed 346-byte member headers, each immediately followed by its member data, stored or zlib compressed.
 */
typedef struct xx_mva {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mva;

typedef xx_mva xx_mva_t;

XXFC_API void xx_mva_init(xx_mva *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_mva *xx_mva_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_mva_destroy(xx_mva *archive);
XXFC_API void xx_mva_free(xx_mva *archive);

XXFC_API bool xx_mva_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_mva_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_mva_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_mva_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mva_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mva_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mva_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mva_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mva_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MVA_H */
