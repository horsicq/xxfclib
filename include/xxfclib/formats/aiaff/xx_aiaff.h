/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_aiaff.h @brief AIX small ("classic") archive reader. */

#ifndef XXFCLIB_FORMAT_AIAFF_H
#define XXFCLIB_FORMAT_AIAFF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An AIX small (AIAFF) archive: a 68-byte file header of blank-padded decimal ASCII offsets, followed by member headers chained through an explicit next-member offset, each carrying its name inline and its payload stored verbatim. The 32-bit sibling of BIGAF -- same structure, 12-character numeric fields instead of 20.
 */
typedef struct xx_aiaff {
    Abstractformat format;
    uint64_t number_of_records;
} xx_aiaff;

typedef xx_aiaff xx_aiaff_t;

XXFC_API void xx_aiaff_init(xx_aiaff *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_aiaff *xx_aiaff_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_aiaff_destroy(xx_aiaff *archive);
XXFC_API void xx_aiaff_free(xx_aiaff *archive);

XXFC_API bool xx_aiaff_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_aiaff_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_aiaff_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_aiaff_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_aiaff_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_aiaff_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_aiaff_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_aiaff_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_aiaff_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AIAFF_H */
