/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sci.h @brief SCI archive reader. */

#ifndef XXFCLIB_FORMAT_SCI_H
#define XXFCLIB_FORMAT_SCI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SCI archive: a 46-byte banner header followed by a chain of 55-byte records, each immediately followed by its member's bytes, stored verbatim.
 */
typedef struct xx_sci {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sci;

typedef xx_sci xx_sci_t;

XXFC_API void xx_sci_init(xx_sci *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sci *xx_sci_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sci_destroy(xx_sci *archive);
XXFC_API void xx_sci_free(xx_sci *archive);

XXFC_API bool xx_sci_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_sci_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sci_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sci_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sci_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sci_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sci_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sci_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sci_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SCI_H */
