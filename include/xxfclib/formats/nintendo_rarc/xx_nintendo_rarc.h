/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nintendo_rarc.h @brief Nintendo RARC archive reader. */

#ifndef XXFCLIB_FORMAT_NINTENDO_RARC_H
#define XXFCLIB_FORMAT_NINTENDO_RARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Nintendo RARC container. */
typedef struct xx_nintendo_rarc {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_nintendo_rarc;

typedef xx_nintendo_rarc xx_nintendo_rarc_t;

XXFC_API void xx_nintendo_rarc_init(xx_nintendo_rarc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_nintendo_rarc *xx_nintendo_rarc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_nintendo_rarc_destroy(xx_nintendo_rarc *archive);
XXFC_API void xx_nintendo_rarc_free(xx_nintendo_rarc *archive);

XXFC_API bool xx_nintendo_rarc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_nintendo_rarc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_nintendo_rarc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_nintendo_rarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nintendo_rarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nintendo_rarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nintendo_rarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_rarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nintendo_rarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NINTENDO_RARC_H */
