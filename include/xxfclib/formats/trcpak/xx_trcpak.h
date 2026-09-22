/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_trcpak.h @brief TRCPAK archive reader. */

#ifndef XXFCLIB_FORMAT_TRCPAK_H
#define XXFCLIB_FORMAT_TRCPAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TRCPAK archive: a seven-byte magic, a float version and a fixed-size directory of stored members, each named in a 0x104 byte field.
 */
typedef struct xx_trcpak {
    Abstractformat format;
    uint64_t number_of_records;
} xx_trcpak;

typedef xx_trcpak xx_trcpak_t;

XXFC_API void xx_trcpak_init(xx_trcpak *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_trcpak *xx_trcpak_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_trcpak_destroy(xx_trcpak *archive);
XXFC_API void xx_trcpak_free(xx_trcpak *archive);

XXFC_API bool xx_trcpak_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_trcpak_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_trcpak_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_trcpak_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_trcpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trcpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trcpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trcpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_trcpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TRCPAK_H */
