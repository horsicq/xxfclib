/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_xar.h @brief XAR archive reader. */

#ifndef XXFCLIB_FORMAT_XAR_H
#define XXFCLIB_FORMAT_XAR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A XAR archive: a 28-byte big-endian header, a zlib-compressed XML table of contents naming every member, and a heap of member payloads whose offsets are relative to the end of that table.
 */
typedef struct xx_xar {
    Abstractformat format;
    uint64_t number_of_records;
} xx_xar;

typedef xx_xar xx_xar_t;

XXFC_API void xx_xar_init(xx_xar *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_xar *xx_xar_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_xar_destroy(xx_xar *archive);
XXFC_API void xx_xar_free(xx_xar *archive);

XXFC_API bool xx_xar_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_xar_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_xar_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_xar_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XAR_H */
