/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_savedskf.h @brief IBM SaveDskF diskette image reader. */

#ifndef XXFCLIB_FORMAT_SAVEDSKF_H
#define XXFCLIB_FORMAT_SAVEDSKF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM SaveDskF diskette image: a 40-byte header describing the
 * floppy's geometry and how many of its sectors were stored, followed by
 * those sectors either verbatim or as one 12-bit LZW stream.
 */
typedef struct xx_savedskf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_savedskf;

typedef xx_savedskf xx_savedskf_t;

XXFC_API void xx_savedskf_init(xx_savedskf *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_savedskf *xx_savedskf_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_savedskf_destroy(xx_savedskf *archive);
XXFC_API void xx_savedskf_free(xx_savedskf *archive);

XXFC_API bool xx_savedskf_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_savedskf_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_savedskf_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_savedskf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_savedskf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_savedskf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_savedskf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_savedskf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_savedskf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SAVEDSKF_H */
