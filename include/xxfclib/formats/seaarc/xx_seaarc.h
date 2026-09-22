/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_seaarc.h @brief SEA ARC archive reader. */

#ifndef XXFCLIB_FORMAT_SEAARC_H
#define XXFCLIB_FORMAT_SEAARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SEA ARC (PKPAK/PKARC) archive: a flat chain of members, each introduced by a 0x1A marker byte and a method byte, terminated by the mandatory 0x1A 0x00 end record.
 */
typedef struct xx_seaarc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_seaarc;

typedef xx_seaarc xx_seaarc_t;

XXFC_API void xx_seaarc_init(xx_seaarc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_seaarc *xx_seaarc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_seaarc_destroy(xx_seaarc *archive);
XXFC_API void xx_seaarc_free(xx_seaarc *archive);

XXFC_API bool xx_seaarc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_seaarc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_seaarc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_seaarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_seaarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_seaarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_seaarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_seaarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_seaarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SEAARC_H */
