/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_TNEF_H
#define XXFCLIB_FORMAT_TNEF_H

#include "xxfclib/formats/xx_format.h"

/* Microsoft Transport Neutral Encapsulation Format (winmail.dat).  The
 * members are the attachments, plus one synthetic member holding the
 * rendered message body. */
typedef struct xx_tnef {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tnef;

XXFC_API void xx_tnef_init(xx_tnef *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_tnef *xx_tnef_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_tnef_destroy(xx_tnef *archive);
XXFC_API void xx_tnef_free(xx_tnef *archive);
XXFC_API bool xx_tnef_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tnef_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_tnef_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_tnef_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_tnef_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tnef_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tnef_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tnef_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tnef_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
