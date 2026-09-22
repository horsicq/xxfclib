/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BINDER_H
#define XXFCLIB_FORMAT_BINDER_H

#include "xxfclib/formats/xx_format.h"

/* Microsoft Office Binder document (.OBD / .OBT / .OBZ).
 *
 * The container is an OLE2 / CFBF (Compound File Binary Format) compound
 * file.  Every section the Binder holds is a storage under the root, and the
 * embedded document lives inside it as ordinary OLE streams.  Members are
 * always stored: extraction only reassembles the FAT or mini FAT chain. */
typedef struct xx_binder {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_binder;

XXFC_API void xx_binder_init(xx_binder *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_binder *xx_binder_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_binder_destroy(xx_binder *archive);
XXFC_API void xx_binder_free(xx_binder *archive);
XXFC_API bool xx_binder_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_binder_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_binder_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_binder_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_binder_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_binder_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_binder_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_binder_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_binder_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
