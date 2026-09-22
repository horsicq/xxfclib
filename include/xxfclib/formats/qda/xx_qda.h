/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_qda.h @brief QDA archive reader. */

#ifndef XXFCLIB_FORMAT_QDA_H
#define XXFCLIB_FORMAT_QDA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A QDA archive: a 0x100-byte header carrying an archive-wide packed flag and a member count, followed by that many fixed 0x10c-byte directory entries.
 */
typedef struct xx_qda {
    Abstractformat format;
    uint64_t number_of_records;
} xx_qda;

typedef xx_qda xx_qda_t;

XXFC_API void xx_qda_init(xx_qda *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_qda *xx_qda_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_qda_destroy(xx_qda *archive);
XXFC_API void xx_qda_free(xx_qda *archive);

XXFC_API bool xx_qda_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_qda_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_qda_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_qda_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qda_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qda_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qda_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qda_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qda_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QDA_H */
