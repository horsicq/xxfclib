/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_qip1.h @brief Quarterdeck QIP install archive reader. */

#ifndef XXFCLIB_FORMAT_QIP1_H
#define XXFCLIB_FORMAT_QIP1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Quarterdeck QIP install archive: a chain of 32-byte 'QD' member records, each followed by its own PKWARE DCL imploded stream, tiling the file exactly from offset 0 to EOF.
 */
typedef struct xx_qip1 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_qip1;

typedef xx_qip1 xx_qip1_t;

XXFC_API void xx_qip1_init(xx_qip1 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_qip1 *xx_qip1_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_qip1_destroy(xx_qip1 *archive);
XXFC_API void xx_qip1_free(xx_qip1 *archive);

XXFC_API bool xx_qip1_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_qip1_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_qip1_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_qip1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qip1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qip1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qip1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qip1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qip1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QIP1_H */
