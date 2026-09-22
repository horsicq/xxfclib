/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_irixsa.h @brief IRIX standalone tools volume reader. */

#ifndef XXFCLIB_FORMAT_IRIXSA_H
#define XXFCLIB_FORMAT_IRIXSA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IRIX standalone tools volume: a single 512-byte header block carrying a fixed table of twenty directory slots, each naming one member stored verbatim at a 512-byte block boundary.
 */
typedef struct xx_irixsa {
    Abstractformat format;
    uint64_t number_of_records;
} xx_irixsa;

typedef xx_irixsa xx_irixsa_t;

XXFC_API void xx_irixsa_init(xx_irixsa *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_irixsa *xx_irixsa_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_irixsa_destroy(xx_irixsa *archive);
XXFC_API void xx_irixsa_free(xx_irixsa *archive);

XXFC_API bool xx_irixsa_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_irixsa_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_irixsa_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_irixsa_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_irixsa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_irixsa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_irixsa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_irixsa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_irixsa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IRIXSA_H */
