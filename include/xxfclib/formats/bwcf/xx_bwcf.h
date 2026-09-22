/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bwcf.h @brief BWCF distribution set reader. */

#ifndef XXFCLIB_FORMAT_BWCF_H
#define XXFCLIB_FORMAT_BWCF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BWCF distribution set: a 0x56 byte header carrying a version and a printable description, followed by member records that alternate a name block and a 0x11 byte descriptor, each trailed by a data block whose members are stored or LZHUF compressed.
 */
typedef struct xx_bwcf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bwcf;

typedef xx_bwcf xx_bwcf_t;

XXFC_API void xx_bwcf_init(xx_bwcf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_bwcf *xx_bwcf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_bwcf_destroy(xx_bwcf *archive);
XXFC_API void xx_bwcf_free(xx_bwcf *archive);

XXFC_API bool xx_bwcf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_bwcf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_bwcf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_bwcf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bwcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bwcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bwcf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bwcf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bwcf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BWCF_H */
