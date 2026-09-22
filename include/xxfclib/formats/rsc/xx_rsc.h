/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rsc.h @brief An ".RSC" resource file: a 16-bit member count followed by a fixed 25-byte-per-member table and contiguous stored payloads. */

#ifndef XXFCLIB_FORMAT_RSC_H
#define XXFCLIB_FORMAT_RSC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ".RSC" resource file: a 16-bit member count followed by a fixed 25-byte-per-member table and contiguous stored payloads.
 */
typedef struct xx_rsc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rsc;

typedef xx_rsc xx_rsc_t;

XXFC_API void xx_rsc_init(xx_rsc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rsc *xx_rsc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rsc_destroy(xx_rsc *archive);
XXFC_API void xx_rsc_free(xx_rsc *archive);

XXFC_API bool xx_rsc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rsc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rsc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rsc_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rsc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rsc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rsc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rsc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rsc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RSC_H */
