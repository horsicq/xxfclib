/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_trc.h @brief TRC archive reader. */

#ifndef XXFCLIB_FORMAT_TRC_H
#define XXFCLIB_FORMAT_TRC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TRCZip archive: a single 0x132 byte header carrying a palindromic 12 byte magic, a second interior magic, a DOS 8.3 name and duplicated CRC and size fields, followed by one PKWARE DCL imploded member.
 */
typedef struct xx_trc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_trc;

typedef xx_trc xx_trc_t;

XXFC_API void xx_trc_init(xx_trc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_trc *xx_trc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_trc_destroy(xx_trc *archive);
XXFC_API void xx_trc_free(xx_trc *archive);

XXFC_API bool xx_trc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_trc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_trc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_trc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_trc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_trc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TRC_H */
