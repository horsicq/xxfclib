/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_IS7INX_H
#define XXFCLIB_FORMAT_IS7INX_H

#include "xxfclib/formats/xx_format.h"

/* InstallShield compiled InstallScript (".inx", also the compiled script
 * embedded in a setup's Script Files).  The whole file is obfuscated; it is
 * a single stream and not a member container, so the reader publishes exactly
 * one record holding the de-obfuscated script. */
typedef struct xx_is7inx {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t script_version;
} xx_is7inx;

XXFC_API void xx_is7inx_init(xx_is7inx *script, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_is7inx *xx_is7inx_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_is7inx_destroy(xx_is7inx *script);
XXFC_API void xx_is7inx_free(xx_is7inx *script);
XXFC_API bool xx_is7inx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_is7inx_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_is7inx_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_is7inx_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_is7inx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_is7inx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_is7inx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_is7inx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_is7inx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
