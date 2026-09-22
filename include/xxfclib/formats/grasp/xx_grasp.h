/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_grasp.h @brief GRASP GL animation library reader. */

#ifndef XXFCLIB_FORMAT_GRASP_H
#define XXFCLIB_FORMAT_GRASP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A GRASP GL library: a 16-bit index size followed by a table of
 * 17-byte (32-bit absolute offset, 13-byte name) entries, each pointing at a
 * length-prefixed stored member.
 */
typedef struct xx_grasp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_grasp;

typedef xx_grasp xx_grasp_t;

XXFC_API void xx_grasp_init(xx_grasp *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_grasp *xx_grasp_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_grasp_destroy(xx_grasp *archive);
XXFC_API void xx_grasp_free(xx_grasp *archive);

XXFC_API bool xx_grasp_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_grasp_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_grasp_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_grasp_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_grasp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_grasp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_grasp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_grasp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_grasp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GRASP_H */
