/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_secondnature.h @brief Second Nature archive reader. */

#ifndef XXFCLIB_FORMAT_SECONDNATURE_H
#define XXFCLIB_FORMAT_SECONDNATURE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Second Nature Software screen-saver module: a 32-byte banner naming one of three directory shapes, whose fixed-width entries point at stored members.
 */
typedef struct xx_secondnature {
    Abstractformat format;
    uint64_t number_of_records;
} xx_secondnature;

typedef xx_secondnature xx_secondnature_t;

XXFC_API void xx_secondnature_init(xx_secondnature *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_secondnature *xx_secondnature_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_secondnature_destroy(xx_secondnature *archive);
XXFC_API void xx_secondnature_free(xx_secondnature *archive);

XXFC_API bool xx_secondnature_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_secondnature_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_secondnature_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_secondnature_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_secondnature_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_secondnature_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_secondnature_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_secondnature_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_secondnature_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SECONDNATURE_H */
