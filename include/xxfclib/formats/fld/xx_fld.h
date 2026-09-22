/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fld.h @brief CodeBase install file group reader. */

#ifndef XXFCLIB_FORMAT_FLD_H
#define XXFCLIB_FORMAT_FLD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CodeBase .FLD install file group: a magicless chain of 27-byte records, each immediately followed by its payload, stored or PKWARE DCL imploded, closing on a 5-byte trailer.
 */
typedef struct xx_fld {
    Abstractformat format;
    uint64_t number_of_records;
} xx_fld;

typedef xx_fld xx_fld_t;

XXFC_API void xx_fld_init(xx_fld *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_fld *xx_fld_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_fld_destroy(xx_fld *archive);
XXFC_API void xx_fld_free(xx_fld *archive);

XXFC_API bool xx_fld_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_fld_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_fld_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_fld_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fld_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fld_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fld_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fld_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fld_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FLD_H */
