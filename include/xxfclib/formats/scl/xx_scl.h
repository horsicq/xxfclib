/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_scl.h @brief ZX Spectrum SCL disk image reader. */

#ifndef XXFCLIB_FORMAT_SCL_H
#define XXFCLIB_FORMAT_SCL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZX Spectrum SCL image: the eight-byte signature "SINCLAIR", a one-byte file count, that many 14-byte TR-DOS catalogue entries, then every file's sectors laid end to end in catalogue order, and normally a u32 byte-sum trailer.
 */
typedef struct xx_scl {
    Abstractformat format;
    uint64_t number_of_records;
} xx_scl;

typedef xx_scl xx_scl_t;

XXFC_API void xx_scl_init(xx_scl *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_scl *xx_scl_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_scl_destroy(xx_scl *archive);
XXFC_API void xx_scl_free(xx_scl *archive);

XXFC_API bool xx_scl_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_scl_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_scl_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_scl_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_scl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_scl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_scl_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_scl_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_scl_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SCL_H */
