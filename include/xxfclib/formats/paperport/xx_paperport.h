/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_paperport.h @brief A Visioneer/ScanSoft PaperPort desktop file: a 200-byte header pointing at a "VZ" chunk tree whose leaves are page image objects. */

#ifndef XXFCLIB_FORMAT_PAPERPORT_H
#define XXFCLIB_FORMAT_PAPERPORT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Visioneer/ScanSoft PaperPort desktop file: a 200-byte header pointing at a "VZ" chunk tree whose leaves are page image objects.
 */
typedef struct xx_paperport {
    Abstractformat format;
    uint64_t number_of_records;
} xx_paperport;

typedef xx_paperport xx_paperport_t;

XXFC_API void xx_paperport_init(xx_paperport *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_paperport *xx_paperport_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_paperport_destroy(xx_paperport *archive);
XXFC_API void xx_paperport_free(xx_paperport *archive);

XXFC_API bool xx_paperport_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_paperport_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_paperport_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_paperport_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_paperport_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_paperport_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_paperport_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_paperport_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_paperport_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PAPERPORT_H */
