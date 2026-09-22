/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bigf.h @brief EA BIG/VIV archive reader. */

#ifndef XXFCLIB_FORMAT_BIGF_H
#define XXFCLIB_FORMAT_BIGF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An EA BIG archive.\n *\n * Electronic Arts' container for game data: a four-byte magic, a count, then a directory of offset/size/name triples with every member stored verbatim. Names are NUL terminated and variable length, so the directory has to be walked rather than indexed.
 */
typedef struct xx_bigf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bigf;

typedef xx_bigf xx_bigf_t;

XXFC_API void xx_bigf_init(xx_bigf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_bigf *xx_bigf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_bigf_destroy(xx_bigf *archive);
XXFC_API void xx_bigf_free(xx_bigf *archive);

XXFC_API bool xx_bigf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_bigf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_bigf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_bigf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bigf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bigf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bigf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bigf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bigf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BIGF_H */
