/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_imp.h @brief IMP archive reader. */

#ifndef XXFCLIB_FORMAT_IMP_H
#define XXFCLIB_FORMAT_IMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IMP archive: a 42-byte header naming a compressed directory, whose records slice members out of one or more solid block streams elsewhere in the file.
 */
typedef struct xx_imp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_imp;

typedef xx_imp xx_imp_t;

XXFC_API void xx_imp_init(xx_imp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_imp *xx_imp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_imp_destroy(xx_imp *archive);
XXFC_API void xx_imp_free(xx_imp *archive);

XXFC_API bool xx_imp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_imp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_imp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_imp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_imp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_imp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_imp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_imp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_imp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IMP_H */
