/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dpk.h @brief DPK archive reader. */

#ifndef XXFCLIB_FORMAT_DPK_H
#define XXFCLIB_FORMAT_DPK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DPK4 archive: a 0x10 byte header carrying the file's own length, followed by a directory of variable-length entries whose members are either stored or zlib compressed.
 */
typedef struct xx_dpk {
    Abstractformat format;
    uint64_t number_of_records;
} xx_dpk;

typedef xx_dpk xx_dpk_t;

XXFC_API void xx_dpk_init(xx_dpk *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_dpk *xx_dpk_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_dpk_destroy(xx_dpk *archive);
XXFC_API void xx_dpk_free(xx_dpk *archive);

XXFC_API bool xx_dpk_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_dpk_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_dpk_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_dpk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dpk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dpk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dpk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DPK_H */
