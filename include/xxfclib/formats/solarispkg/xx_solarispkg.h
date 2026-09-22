/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_SOLARISPKG_H
#define XXFCLIB_FORMAT_SOLARISPKG_H

#include "xxfclib/formats/xx_format.h"

/* SVR4 / Solaris package datastream (pkgtrans, pkgadd).  A 512-byte ASCII
 * header block followed by a chain of uncompressed cpio archives. */
typedef struct xx_solarispkg {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_solarispkg;

XXFC_API void xx_solarispkg_init(xx_solarispkg *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_solarispkg *xx_solarispkg_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_solarispkg_destroy(xx_solarispkg *archive);
XXFC_API void xx_solarispkg_free(xx_solarispkg *archive);
XXFC_API bool xx_solarispkg_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_solarispkg_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_solarispkg_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_solarispkg_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_solarispkg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_solarispkg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_solarispkg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_solarispkg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_solarispkg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
