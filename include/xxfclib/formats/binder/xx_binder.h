/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BINDER_H
#define XXFCLIB_FORMAT_BINDER_H

#include "xxfclib/formats/xx_format.h"

/* OLE2 / CFBF (Compound File Binary Format) compound files.  One parser,
 * two faces that split compound files between them without overlap:
 *
 * xx_binder: Microsoft Office Binder document (.OBD / .OBT / .OBZ), told
 * apart by the Binder root class id plus a root-level "Binder" stream.
 * Every section the Binder holds is a storage under the root, and the
 * embedded document lives inside it as ordinary OLE streams.
 *
 * xx_cfbf: every other well-formed compound file (MSI / MSP databases,
 * .doc / .xls / .ppt 97-2003, .msg, Thumbs.db, ...).  Streams are the
 * members, named by storage path; MSI's packed stream names are expanded
 * and control characters are spelled "[N]" as 7-Zip does.
 *
 * Members are always stored: extraction only reassembles the FAT or mini FAT
 * chain. */
typedef struct xx_binder {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_binder;

typedef struct xx_cfbf {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_cfbf;

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

XXFC_API void xx_cfbf_init(xx_cfbf *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_cfbf *xx_cfbf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cfbf_destroy(xx_cfbf *archive);
XXFC_API void xx_cfbf_free(xx_cfbf *archive);
XXFC_API bool xx_cfbf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cfbf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cfbf_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_cfbf_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_cfbf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cfbf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cfbf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cfbf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cfbf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
