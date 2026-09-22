/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_chieflz.h @brief ChiefLZ archive reader. */

#ifndef XXFCLIB_FORMAT_CHIEFLZ_H
#define XXFCLIB_FORMAT_CHIEFLZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ChiefLZ Single archive: one compressed member behind a fixed 257-byte header that carries the original name, the plaintext length and a CRC32.
 */
typedef struct xx_chieflz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_chieflz;

typedef xx_chieflz xx_chieflz_t;

XXFC_API void xx_chieflz_init(xx_chieflz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_chieflz *xx_chieflz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_chieflz_destroy(xx_chieflz *archive);
XXFC_API void xx_chieflz_free(xx_chieflz *archive);

XXFC_API bool xx_chieflz_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_chieflz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_chieflz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_chieflz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_chieflz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_chieflz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_chieflz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_chieflz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_chieflz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CHIEFLZ_H */
