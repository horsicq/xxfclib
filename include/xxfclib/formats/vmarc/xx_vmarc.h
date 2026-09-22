/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_vmarc.h @brief VMARC archive reader. */

#ifndef XXFCLIB_FORMAT_VMARC_H
#define XXFCLIB_FORMAT_VMARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A VM/CMS VMARC archive: a chain of 38-byte EBCDIC member headers on 80-byte boundaries, each followed by a member whose length is stored nowhere and has to be measured by decoding it.
 */
typedef struct xx_vmarc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_vmarc;

typedef xx_vmarc xx_vmarc_t;

XXFC_API void xx_vmarc_init(xx_vmarc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_vmarc *xx_vmarc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_vmarc_destroy(xx_vmarc *archive);
XXFC_API void xx_vmarc_free(xx_vmarc *archive);

XXFC_API bool xx_vmarc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_vmarc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_vmarc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_vmarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_vmarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VMARC_H */
