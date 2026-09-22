/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_spis.h @brief SPIS archive reader. */

#ifndef XXFCLIB_FORMAT_SPIS_H
#define XXFCLIB_FORMAT_SPIS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SPIS archive: GP-Install's bounded payload container, either a single compressed stream or a chain of independently compressed named members, each authenticated by a 32-bit sum of its decompressed bytes.
 */
typedef struct xx_spis {
    Abstractformat format;
    uint64_t number_of_records;
} xx_spis;

typedef xx_spis xx_spis_t;

XXFC_API void xx_spis_init(xx_spis *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_spis *xx_spis_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_spis_destroy(xx_spis *archive);
XXFC_API void xx_spis_free(xx_spis *archive);

XXFC_API bool xx_spis_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_spis_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_spis_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_spis_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_spis_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_spis_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_spis_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_spis_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_spis_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SPIS_H */
