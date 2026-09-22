/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bigaf.h @brief AIX big archive reader. */

#ifndef XXFCLIB_FORMAT_BIGAF_H
#define XXFCLIB_FORMAT_BIGAF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An AIX "big" (BIGAF) archive: a fixed 128-byte file header whose every numeric field is blank-padded decimal ASCII, followed by member headers chained through an explicit next-member offset, each carrying its name inline and its payload stored verbatim.
 */
typedef struct xx_bigaf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bigaf;

typedef xx_bigaf xx_bigaf_t;

XXFC_API void xx_bigaf_init(xx_bigaf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_bigaf *xx_bigaf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_bigaf_destroy(xx_bigaf *archive);
XXFC_API void xx_bigaf_free(xx_bigaf *archive);

XXFC_API bool xx_bigaf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_bigaf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_bigaf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_bigaf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bigaf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bigaf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bigaf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bigaf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bigaf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BIGAF_H */
