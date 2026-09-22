/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ti99arc.h @brief TI-99/4A ARC archive reader. */

#ifndef XXFCLIB_FORMAT_TI99ARC_H
#define XXFCLIB_FORMAT_TI99ARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TI-99/4A ARC (.ARK) archive: a chain of 256-byte catalogue sectors, each holding fourteen 18-byte entries and a four-byte link field, followed by every member's sectors - the whole payload optionally wrapped in a TIFILES or FIAD header and optionally LZW compressed as a single stream.
 */
typedef struct xx_ti99arc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ti99arc;

typedef xx_ti99arc xx_ti99arc_t;

XXFC_API void xx_ti99arc_init(xx_ti99arc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ti99arc *xx_ti99arc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ti99arc_destroy(xx_ti99arc *archive);
XXFC_API void xx_ti99arc_free(xx_ti99arc *archive);

XXFC_API bool xx_ti99arc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ti99arc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ti99arc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ti99arc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ti99arc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ti99arc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ti99arc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ti99arc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ti99arc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TI99ARC_H */
