/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ascendbackup.h @brief Ascend backup volume reader. */

#ifndef XXFCLIB_FORMAT_ASCENDBACKUP_H
#define XXFCLIB_FORMAT_ASCENDBACKUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Ascend backup volume: a magic-less chain of records, each a u16 name length, a DOS 8.3 name, a u32 packed size and a PKWARE DCL imploded payload, tiling the file exactly to its end.
 */
typedef struct xx_ascendbackup {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ascendbackup;

typedef xx_ascendbackup xx_ascendbackup_t;

XXFC_API void xx_ascendbackup_init(xx_ascendbackup *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ascendbackup *xx_ascendbackup_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ascendbackup_destroy(xx_ascendbackup *archive);
XXFC_API void xx_ascendbackup_free(xx_ascendbackup *archive);

XXFC_API bool xx_ascendbackup_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ascendbackup_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ascendbackup_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ascendbackup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ascendbackup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ascendbackup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ascendbackup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ascendbackup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ascendbackup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ASCENDBACKUP_H */
