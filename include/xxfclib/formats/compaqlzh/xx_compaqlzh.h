/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_COMPAQLZH_H
#define XXFCLIB_FORMAT_COMPAQLZH_H

#include "xxfclib/formats/xx_format.h"

/* Compaq's "CPQ_LZH" driver/firmware distribution wrapper: a fixed 29-byte
 * header naming exactly one DOS member, followed by a raw LHA -lh1- stream
 * that runs to the end of the file. */
typedef struct xx_compaqlzh {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_compaqlzh;

XXFC_API void xx_compaqlzh_init(xx_compaqlzh *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_compaqlzh *xx_compaqlzh_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_compaqlzh_destroy(xx_compaqlzh *archive);
XXFC_API void xx_compaqlzh_free(xx_compaqlzh *archive);
XXFC_API bool xx_compaqlzh_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_compaqlzh_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_compaqlzh_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_compaqlzh_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_compaqlzh_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_compaqlzh_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_compaqlzh_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_compaqlzh_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_compaqlzh_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
