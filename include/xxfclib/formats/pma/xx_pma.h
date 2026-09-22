/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PMA_H
#define XXFCLIB_FORMAT_PMA_H

#include "xxfclib/formats/xx_format.h"

/* PMarc (PMA) archives: the CP/M and MSX cousin of LHA.  Members carry the
 * "-pm0-", "-pm1-" and "-pm2-" method tags inside an LHA level-0 header. */
typedef struct xx_pma {
    Abstractformat format;
    uint64_t number_of_records;
} xx_pma;

XXFC_API void xx_pma_init(xx_pma *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pma *xx_pma_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pma_destroy(xx_pma *archive);
XXFC_API void xx_pma_free(xx_pma *archive);
XXFC_API bool xx_pma_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pma_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pma_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pma_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_pma_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pma_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pma_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pma_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pma_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
