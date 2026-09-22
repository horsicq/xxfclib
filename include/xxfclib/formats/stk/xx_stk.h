/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_stk.h @brief Coktel Vision STK archive reader. */

#ifndef XXFCLIB_FORMAT_STK_H
#define XXFCLIB_FORMAT_STK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Coktel Vision STK/ITK data archive: either the classic headerless generation, a 16-bit member count followed by fixed 22-byte directory entries, or the "STK2." generation, whose header points at a directory of packed names and fixed-stride metadata records placed after the member data.
 */
typedef struct xx_stk {
    Abstractformat format;
    uint64_t number_of_records;
} xx_stk;

typedef xx_stk xx_stk_t;

XXFC_API void xx_stk_init(xx_stk *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_stk *xx_stk_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_stk_destroy(xx_stk *archive);
XXFC_API void xx_stk_free(xx_stk *archive);

XXFC_API bool xx_stk_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_stk_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_stk_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_stk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STK_H */
