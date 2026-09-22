/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_agis.h @brief An AGIS archive: a chain of 31-byte "AGIS" headers, each followed by a PKWARE DCL Implode stream or a stored payload. */

#ifndef XXFCLIB_FORMAT_AGIS_H
#define XXFCLIB_FORMAT_AGIS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An AGIS archive: a chain of 31-byte "AGIS" headers, each followed by a PKWARE DCL Implode stream or a stored payload.
 */
typedef struct xx_agis {
    Abstractformat format;
    uint64_t number_of_records;
} xx_agis;

typedef xx_agis xx_agis_t;

XXFC_API void xx_agis_init(xx_agis *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_agis *xx_agis_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_agis_destroy(xx_agis *archive);
XXFC_API void xx_agis_free(xx_agis *archive);

XXFC_API bool xx_agis_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_agis_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_agis_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_agis_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_agis_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_agis_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_agis_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_agis_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_agis_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AGIS_H */
