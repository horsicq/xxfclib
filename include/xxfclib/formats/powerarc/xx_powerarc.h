/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_powerarc.h @brief PowerArc archive reader. */

#ifndef XXFCLIB_FORMAT_POWERARC_H
#define XXFCLIB_FORMAT_POWERARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PowerArc archive: an eight-byte "BZIP0001" wrapper in front of a single, complete bzip2 stream that runs to the end of the file, so the container holds exactly one member and stores neither its name nor its uncompressed size.
 */
typedef struct xx_powerarc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_powerarc;

typedef xx_powerarc xx_powerarc_t;

XXFC_API void xx_powerarc_init(xx_powerarc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_powerarc *xx_powerarc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_powerarc_destroy(xx_powerarc *archive);
XXFC_API void xx_powerarc_free(xx_powerarc *archive);

XXFC_API bool xx_powerarc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_powerarc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_powerarc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_powerarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_powerarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_powerarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_powerarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_powerarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_powerarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_POWERARC_H */
