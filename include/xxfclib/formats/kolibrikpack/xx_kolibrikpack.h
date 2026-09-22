/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_kolibrikpack.h @brief KolibriOS kpack container reader. */

#ifndef XXFCLIB_FORMAT_KOLIBRIKPACK_H
#define XXFCLIB_FORMAT_KOLIBRIKPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A KolibriOS kpack container: a 12-byte header naming the plaintext length and the filter flags, followed by a headerless LZMA1 stream that runs to end of file.
 */
typedef struct xx_kolibrikpack {
    Abstractformat format;
    uint64_t number_of_records;
} xx_kolibrikpack;

typedef xx_kolibrikpack xx_kolibrikpack_t;

XXFC_API void xx_kolibrikpack_init(xx_kolibrikpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_kolibrikpack *xx_kolibrikpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_kolibrikpack_destroy(xx_kolibrikpack *archive);
XXFC_API void xx_kolibrikpack_free(xx_kolibrikpack *archive);

XXFC_API bool xx_kolibrikpack_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_kolibrikpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_kolibrikpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_kolibrikpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_kolibrikpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kolibrikpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kolibrikpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kolibrikpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kolibrikpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_KOLIBRIKPACK_H */
