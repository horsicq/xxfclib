/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sabdu.h @brief A SAB Diskette Utility image (".sdu"): a 46-byte header naming the disk geometry, followed by the raw sector image. */

#ifndef XXFCLIB_FORMAT_SABDU_H
#define XXFCLIB_FORMAT_SABDU_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SAB Diskette Utility image (".sdu"): a 46-byte header naming the disk geometry, followed by the raw sector image.
 */
typedef struct xx_sabdu {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sabdu;

typedef xx_sabdu xx_sabdu_t;

XXFC_API void xx_sabdu_init(xx_sabdu *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sabdu *xx_sabdu_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sabdu_destroy(xx_sabdu *archive);
XXFC_API void xx_sabdu_free(xx_sabdu *archive);

XXFC_API bool xx_sabdu_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sabdu_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sabdu_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sabdu_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sabdu_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sabdu_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sabdu_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sabdu_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sabdu_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SABDU_H */
