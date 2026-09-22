/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pcommos2.h @brief IBM PCOMM for OS/2 packed file reader. */

#ifndef XXFCLIB_FORMAT_PCOMMOS2_H
#define XXFCLIB_FORMAT_PCOMMOS2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A single packed file from an IBM Personal Communications for OS/2 install diskette: a bare LZ77 token stream with no header, no name and no stored size.
 */
typedef struct xx_pcommos2 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_pcommos2;

typedef xx_pcommos2 xx_pcommos2_t;

XXFC_API void xx_pcommos2_init(xx_pcommos2 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_pcommos2 *xx_pcommos2_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_pcommos2_destroy(xx_pcommos2 *archive);
XXFC_API void xx_pcommos2_free(xx_pcommos2 *archive);

XXFC_API bool xx_pcommos2_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_pcommos2_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_pcommos2_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_pcommos2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pcommos2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pcommos2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pcommos2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pcommos2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pcommos2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCOMMOS2_H */
