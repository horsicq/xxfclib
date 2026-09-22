/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ixa.h @brief An IXALANCE ".ixa" container: a 0x40-byte header with an embedded title, a 12-byte-per-member table of (offset, packed, plain) triples, and a contiguous compressed payload chain. */

#ifndef XXFCLIB_FORMAT_IXA_H
#define XXFCLIB_FORMAT_IXA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IXALANCE ".ixa" container: a 0x40-byte header with an embedded title, a 12-byte-per-member table of (offset, packed, plain) triples, and a contiguous compressed payload chain.
 */
typedef struct xx_ixa {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ixa;

typedef xx_ixa xx_ixa_t;

XXFC_API void xx_ixa_init(xx_ixa *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ixa *xx_ixa_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ixa_destroy(xx_ixa *archive);
XXFC_API void xx_ixa_free(xx_ixa *archive);

XXFC_API bool xx_ixa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ixa_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ixa_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ixa_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ixa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ixa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ixa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ixa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ixa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IXA_H */
