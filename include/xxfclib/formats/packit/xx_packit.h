/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_packit.h @brief PackIt archive reader. */

#ifndef XXFCLIB_FORMAT_PACKIT_H
#define XXFCLIB_FORMAT_PACKIT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PackIt archive: a sixteen-byte banner, then a chain of tagged member records ending in a terminator tag.
 */
typedef struct xx_packit {
    Abstractformat format;
    uint64_t number_of_records;
} xx_packit;

typedef xx_packit xx_packit_t;

XXFC_API void xx_packit_init(xx_packit *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_packit *xx_packit_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_packit_destroy(xx_packit *archive);
XXFC_API void xx_packit_free(xx_packit *archive);

XXFC_API bool xx_packit_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_packit_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_packit_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_packit_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_packit_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_packit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_packit_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_packit_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_packit_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PACKIT_H */
