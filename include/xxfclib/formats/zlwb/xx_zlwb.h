/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zlwb.h @brief ZLWB archive reader. */

#ifndef XXFCLIB_FORMAT_ZLWB_H
#define XXFCLIB_FORMAT_ZLWB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZLWB archive: a 0x1e-byte header pointing at a directory whose every entry is itself a zlib stream inflating to a fixed-size Delphi record, with the member data deflated as zlib as well.
 */
typedef struct xx_zlwb {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zlwb;

typedef xx_zlwb xx_zlwb_t;

XXFC_API void xx_zlwb_init(xx_zlwb *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zlwb *xx_zlwb_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zlwb_destroy(xx_zlwb *archive);
XXFC_API void xx_zlwb_free(xx_zlwb *archive);

XXFC_API bool xx_zlwb_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zlwb_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zlwb_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zlwb_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zlwb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zlwb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zlwb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zlwb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zlwb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZLWB_H */
