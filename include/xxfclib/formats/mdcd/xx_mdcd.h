/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MDCD_H
#define XXFCLIB_FORMAT_MDCD_H

#include "xxfclib/formats/xx_format.h"

/* MDCD archive (Mass Data CD builder); stored or Zoo-LZD members. */
typedef struct xx_mdcd {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_mdcd;

XXFC_API void xx_mdcd_init(xx_mdcd *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_mdcd *xx_mdcd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mdcd_destroy(xx_mdcd *archive);
XXFC_API void xx_mdcd_free(xx_mdcd *archive);
XXFC_API bool xx_mdcd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mdcd_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mdcd_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mdcd_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mdcd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mdcd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mdcd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mdcd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mdcd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
