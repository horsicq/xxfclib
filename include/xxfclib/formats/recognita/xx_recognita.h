/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_RECOGNITA_H
#define XXFCLIB_FORMAT_RECOGNITA_H

#include "xxfclib/formats/xx_format.h"

/* Recognita OCR distribution archive (.CMP): a headerless chain of 25-byte
 * member headers whose payloads are PKWARE DCL imploded. */
typedef struct xx_recognita {
    Abstractformat format;
    uint64_t number_of_records;
} xx_recognita;

XXFC_API void xx_recognita_init(xx_recognita *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_recognita *xx_recognita_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_recognita_destroy(xx_recognita *archive);
XXFC_API void xx_recognita_free(xx_recognita *archive);
XXFC_API bool xx_recognita_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_recognita_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_recognita_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_recognita_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_recognita_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_recognita_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_recognita_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_recognita_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_recognita_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
