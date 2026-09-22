/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fmc1.h @brief Form Master FMC1 archive reader. */

#ifndef XXFCLIB_FORMAT_FMC1_H
#define XXFCLIB_FORMAT_FMC1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Form Master FMC1 archive: a four-byte magic followed by a chain of 24-byte records, each immediately followed by its Okumura LZSS payload, tiling the file exactly.
 */
typedef struct xx_fmc1 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_fmc1;

typedef xx_fmc1 xx_fmc1_t;

XXFC_API void xx_fmc1_init(xx_fmc1 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_fmc1 *xx_fmc1_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_fmc1_destroy(xx_fmc1 *archive);
XXFC_API void xx_fmc1_free(xx_fmc1 *archive);

XXFC_API bool xx_fmc1_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_fmc1_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_fmc1_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_fmc1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fmc1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fmc1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fmc1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fmc1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fmc1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FMC1_H */
