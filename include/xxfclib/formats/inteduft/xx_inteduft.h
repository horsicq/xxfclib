/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_inteduft.h @brief INTEDUFT resource pack reader. */

#ifndef XXFCLIB_FORMAT_INTEDUFT_H
#define XXFCLIB_FORMAT_INTEDUFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An INTEDUFT resource pack: a six-byte header, a packed index of variable-length names each pointing at a 16-byte member header, and member payloads that are either stored or raw Deflate.
 */
typedef struct xx_inteduft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_inteduft;

typedef xx_inteduft xx_inteduft_t;

XXFC_API void xx_inteduft_init(xx_inteduft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_inteduft *xx_inteduft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_inteduft_destroy(xx_inteduft *archive);
XXFC_API void xx_inteduft_free(xx_inteduft *archive);

XXFC_API bool xx_inteduft_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_inteduft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_inteduft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_inteduft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_inteduft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_inteduft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_inteduft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_inteduft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_inteduft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INTEDUFT_H */
