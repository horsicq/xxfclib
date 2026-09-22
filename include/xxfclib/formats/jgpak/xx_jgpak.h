/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_jgpak.h @brief JGPAK archive reader. */

#ifndef XXFCLIB_FORMAT_JGPAK_H
#define XXFCLIB_FORMAT_JGPAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JGPAK archive: a 'JGPAK\0\1' signature, two length-prefixed banner strings and a member count, followed by a directory of variable-length records and an LZHUF payload area the records must tile exactly.
 */
typedef struct xx_jgpak {
    Abstractformat format;
    uint64_t number_of_records;
} xx_jgpak;

typedef xx_jgpak xx_jgpak_t;

XXFC_API void xx_jgpak_init(xx_jgpak *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_jgpak *xx_jgpak_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_jgpak_destroy(xx_jgpak *archive);
XXFC_API void xx_jgpak_free(xx_jgpak *archive);

XXFC_API bool xx_jgpak_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_jgpak_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_jgpak_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_jgpak_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jgpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jgpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jgpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jgpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jgpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JGPAK_H */
