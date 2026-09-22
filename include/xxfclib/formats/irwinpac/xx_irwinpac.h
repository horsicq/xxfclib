/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_irwinpac.h @brief IrwinPac installation file reader. */

#ifndef XXFCLIB_FORMAT_IRWINPAC_H
#define XXFCLIB_FORMAT_IRWINPAC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Irwin Magnetic Systems IrwinPac installation file: a 20-byte header followed by a chain of independent blocks, each stored or LZ77 packed, together forming one unnamed member.
 */
typedef struct xx_irwinpac {
    Abstractformat format;
    uint64_t number_of_records;
} xx_irwinpac;

typedef xx_irwinpac xx_irwinpac_t;

XXFC_API void xx_irwinpac_init(xx_irwinpac *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_irwinpac *xx_irwinpac_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_irwinpac_destroy(xx_irwinpac *archive);
XXFC_API void xx_irwinpac_free(xx_irwinpac *archive);

XXFC_API bool xx_irwinpac_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_irwinpac_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_irwinpac_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_irwinpac_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_irwinpac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_irwinpac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_irwinpac_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_irwinpac_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_irwinpac_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IRWINPAC_H */
