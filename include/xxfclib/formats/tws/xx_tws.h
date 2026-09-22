/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_tws.h @brief TWS archive reader. */

#ifndef XXFCLIB_FORMAT_TWS_H
#define XXFCLIB_FORMAT_TWS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TWS archive: a table of fixed 25-byte records, the first of which is the archive header and the rest members.
 */
typedef struct xx_tws {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tws;

typedef xx_tws xx_tws_t;

XXFC_API void xx_tws_init(xx_tws *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_tws *xx_tws_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_tws_destroy(xx_tws *archive);
XXFC_API void xx_tws_free(xx_tws *archive);

XXFC_API bool xx_tws_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_tws_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_tws_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_tws_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tws_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tws_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tws_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tws_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tws_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TWS_H */
