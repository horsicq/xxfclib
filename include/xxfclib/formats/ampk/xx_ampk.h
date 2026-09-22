/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_AMPK_H
#define XXFCLIB_FORMAT_AMPK_H

#include "xxfclib/formats/xx_format.h"

typedef struct xx_ampk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint8_t version;
    bool is_complete;
} xx_ampk;

XXFC_API void xx_ampk_init(xx_ampk *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_ampk *xx_ampk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ampk_destroy(xx_ampk *archive);
XXFC_API void xx_ampk_free(xx_ampk *archive);
XXFC_API bool xx_ampk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ampk_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_ampk_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_ampk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_ampk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ampk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ampk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ampk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ampk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
