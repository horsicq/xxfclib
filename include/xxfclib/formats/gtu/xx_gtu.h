/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gtu.h @brief GTU distribution kit reader. */

#ifndef XXFCLIB_FORMAT_GTU_H
#define XXFCLIB_FORMAT_GTU_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM OS/2 GTU corrective-service kit: a chained index of obfuscated member names, each pointing at a data block that repeats its own index record and carries the member's plaintext as a chain of Okumura LZARI frames.
 */
typedef struct xx_gtu {
    Abstractformat format;
    uint64_t number_of_records;
} xx_gtu;

typedef xx_gtu xx_gtu_t;

XXFC_API void xx_gtu_init(xx_gtu *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_gtu *xx_gtu_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_gtu_destroy(xx_gtu *archive);
XXFC_API void xx_gtu_free(xx_gtu *archive);

XXFC_API bool xx_gtu_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_gtu_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_gtu_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_gtu_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gtu_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gtu_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gtu_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gtu_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gtu_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GTU_H */
