/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_stunts.h @brief Stunts / 4D Sports Driving DSI compressed resource reader. */

#ifndef XXFCLIB_FORMAT_STUNTS_H
#define XXFCLIB_FORMAT_STUNTS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Stunts (4D Sports Driving) DSI resource: a four-byte header
 * carrying a pass count and the 24-bit plaintext length, followed by that
 * many chained RLE and variable-length-code passes.  It wraps exactly one
 * payload, so the record list always holds one entry.
 */
typedef struct xx_stunts {
    Abstractformat format;
    uint64_t number_of_records;
} xx_stunts;

typedef xx_stunts xx_stunts_t;

XXFC_API void xx_stunts_init(xx_stunts *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_stunts *xx_stunts_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stunts_destroy(xx_stunts *archive);
XXFC_API void xx_stunts_free(xx_stunts *archive);

XXFC_API bool xx_stunts_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_stunts_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_stunts_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_stunts_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stunts_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stunts_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stunts_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stunts_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stunts_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STUNTS_H */
