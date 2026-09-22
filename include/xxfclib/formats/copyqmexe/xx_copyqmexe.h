/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_COPYQMEXE_H
#define XXFCLIB_FORMAT_COPYQMEXE_H

#include "xxfclib/formats/xx_format.h"

/* Sydex CopyQM-family DOS tool with a "TX" help-text overlay: a Huffman node table, a screen directory and packed line records. */
typedef struct xx_copyqmexe {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_copyqmexe;

XXFC_API void xx_copyqmexe_init(xx_copyqmexe *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_copyqmexe *xx_copyqmexe_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_copyqmexe_destroy(xx_copyqmexe *archive);
XXFC_API void xx_copyqmexe_free(xx_copyqmexe *archive);
XXFC_API bool xx_copyqmexe_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_copyqmexe_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_copyqmexe_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_copyqmexe_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_copyqmexe_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_copyqmexe_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_copyqmexe_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_copyqmexe_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_copyqmexe_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
