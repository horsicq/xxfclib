/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pcxlib.h @brief Genus PCX Library (pcxLib) container reader. */

#ifndef XXFCLIB_FORMAT_PCXLIB_H
#define XXFCLIB_FORMAT_PCXLIB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Genus Microprogramming PCX Library: a 122-byte banner header
 * followed by a chain of 84-byte member headers, each immediately followed
 * by its stored body.
 */
typedef struct xx_pcxlib {
    Abstractformat format;
    uint64_t number_of_records;
} xx_pcxlib;

typedef xx_pcxlib xx_pcxlib_t;

XXFC_API void xx_pcxlib_init(xx_pcxlib *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_pcxlib *xx_pcxlib_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pcxlib_destroy(xx_pcxlib *archive);
XXFC_API void xx_pcxlib_free(xx_pcxlib *archive);

XXFC_API bool xx_pcxlib_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pcxlib_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pcxlib_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_pcxlib_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pcxlib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pcxlib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pcxlib_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pcxlib_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pcxlib_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCXLIB_H */
