/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_glu.h @brief GLU archive reader. */

#ifndef XXFCLIB_FORMAT_GLU_H
#define XXFCLIB_FORMAT_GLU_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A GLU archive: a bare chain of NUL-terminated DOS names, each followed immediately by an LZW15V code stream, with no header, no magic, no directory and no stored sizes anywhere.
 */
typedef struct xx_glu {
    Abstractformat format;
    uint64_t number_of_records;
} xx_glu;

typedef xx_glu xx_glu_t;

XXFC_API void xx_glu_init(xx_glu *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_glu *xx_glu_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_glu_destroy(xx_glu *archive);
XXFC_API void xx_glu_free(xx_glu *archive);

XXFC_API bool xx_glu_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_glu_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_glu_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_glu_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_glu_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_glu_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_glu_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_glu_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_glu_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GLU_H */
