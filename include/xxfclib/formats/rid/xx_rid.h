/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rid.h @brief RID installer archive reader. */

#ifndef XXFCLIB_FORMAT_RID_H
#define XXFCLIB_FORMAT_RID_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A RID OS/2 installer package: a chain of 0x2b-byte member headers, each followed by a chain of framed stored or PKWARE DCL blocks terminated by an end frame.
 */
typedef struct xx_rid {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rid;

typedef xx_rid xx_rid_t;

XXFC_API void xx_rid_init(xx_rid *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rid *xx_rid_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rid_destroy(xx_rid *archive);
XXFC_API void xx_rid_free(xx_rid *archive);

XXFC_API bool xx_rid_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_rid_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rid_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rid_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rid_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rid_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rid_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rid_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rid_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RID_H */
