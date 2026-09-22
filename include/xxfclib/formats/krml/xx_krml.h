/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_krml.h @brief KRML game resource archive reader. */

#ifndef XXFCLIB_FORMAT_KRML_H
#define XXFCLIB_FORMAT_KRML_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A KRML game resource archive: a 6-byte header, a flat fixed-width directory, and stored member data.
 */
typedef struct xx_krml {
    Abstractformat format;
    uint64_t number_of_records;
} xx_krml;

typedef xx_krml xx_krml_t;

XXFC_API void xx_krml_init(xx_krml *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_krml *xx_krml_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_krml_destroy(xx_krml *archive);
XXFC_API void xx_krml_free(xx_krml *archive);

XXFC_API bool xx_krml_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_krml_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_krml_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_krml_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_krml_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_krml_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_krml_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_krml_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_krml_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_KRML_H */
