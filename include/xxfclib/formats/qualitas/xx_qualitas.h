/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_qualitas.h @brief Qualitas install disk reader. */

#ifndef XXFCLIB_FORMAT_QUALITAS_H
#define XXFCLIB_FORMAT_QUALITAS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Qualitas install disk image: a 14 byte header with no magic, a self-chaining directory of variable-length records, and PKWARE DCL imploded member streams each prefixed by a 4 byte CRC word.
 */
typedef struct xx_qualitas {
    Abstractformat format;
    uint64_t number_of_records;
} xx_qualitas;

typedef xx_qualitas xx_qualitas_t;

XXFC_API void xx_qualitas_init(xx_qualitas *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_qualitas *xx_qualitas_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_qualitas_destroy(xx_qualitas *archive);
XXFC_API void xx_qualitas_free(xx_qualitas *archive);

XXFC_API bool xx_qualitas_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_qualitas_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_qualitas_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_qualitas_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qualitas_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qualitas_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qualitas_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qualitas_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qualitas_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QUALITAS_H */
