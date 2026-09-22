/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zzz.h @brief A ".ZZZ" archive: a chain of 24-byte "ZZZ" headers each followed by a PKWARE DCL Implode stream. */

#ifndef XXFCLIB_FORMAT_ZZZ_H
#define XXFCLIB_FORMAT_ZZZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ".ZZZ" archive: a chain of 24-byte "ZZZ" headers each followed by a PKWARE DCL Implode stream.
 */
typedef struct xx_zzz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zzz;

typedef xx_zzz xx_zzz_t;

XXFC_API void xx_zzz_init(xx_zzz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zzz *xx_zzz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zzz_destroy(xx_zzz *archive);
XXFC_API void xx_zzz_free(xx_zzz *archive);

XXFC_API bool xx_zzz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zzz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zzz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zzz_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zzz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zzz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zzz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zzz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zzz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZZZ_H */
