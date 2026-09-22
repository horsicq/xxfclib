/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_ALZ_H
#define XXFCLIB_FORMAT_ALZ_H

#include "xxfclib/formats/xx_format.h"

/* ALZip's ALZ container.  Entries may use Store, BZip2, or raw Deflate. */
typedef struct xx_alz {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_alz;

XXFC_API void xx_alz_init(xx_alz *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_alz *xx_alz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_alz_destroy(xx_alz *archive);
XXFC_API void xx_alz_free(xx_alz *archive);
XXFC_API bool xx_alz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_alz_handle_base_info(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API int64_t xx_alz_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_alz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_alz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_alz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_alz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_alz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_alz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
