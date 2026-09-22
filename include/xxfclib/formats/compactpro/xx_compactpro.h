/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_compactpro.h @brief Compact Pro archive reader. */

#ifndef XXFCLIB_FORMAT_COMPACTPRO_H
#define XXFCLIB_FORMAT_COMPACTPRO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Compact Pro (.cpt) archive: member data first, then a CRC-protected catalogue of nested directory and file records, each file carrying a resource fork and a data fork.
 */
typedef struct xx_compactpro {
    Abstractformat format;
    uint64_t number_of_records;
} xx_compactpro;

typedef xx_compactpro xx_compactpro_t;

XXFC_API void xx_compactpro_init(xx_compactpro *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_compactpro *xx_compactpro_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_compactpro_destroy(xx_compactpro *archive);
XXFC_API void xx_compactpro_free(xx_compactpro *archive);

XXFC_API bool xx_compactpro_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_compactpro_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_compactpro_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_compactpro_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_compactpro_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_compactpro_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_compactpro_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_compactpro_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_compactpro_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_COMPACTPRO_H */
