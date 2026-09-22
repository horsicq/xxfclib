/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fiz.h @brief Maximus FIZ archive reader. */

#ifndef XXFCLIB_FORMAT_FIZ_H
#define XXFCLIB_FORMAT_FIZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Maximus FIZ archive: a headerless chain of members, each a 20-byte record carrying its own "FIZ\x1a" magic, followed by a variable-length name and then the member's stored or LHA -lh5- payload.
 */
typedef struct xx_fiz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_fiz;

typedef xx_fiz xx_fiz_t;

XXFC_API void xx_fiz_init(xx_fiz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_fiz *xx_fiz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_fiz_destroy(xx_fiz *archive);
XXFC_API void xx_fiz_free(xx_fiz *archive);

XXFC_API bool xx_fiz_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_fiz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_fiz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_fiz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fiz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fiz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fiz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fiz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fiz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FIZ_H */
