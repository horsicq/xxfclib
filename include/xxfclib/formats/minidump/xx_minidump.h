/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_minidump.h @brief Windows MiniDump reader. */

#ifndef XXFCLIB_FORMAT_MINIDUMP_H
#define XXFCLIB_FORMAT_MINIDUMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Windows MiniDump: a 32 byte header pointing at a flat stream directory whose entries each locate one stored, uncompressed stream elsewhere in the file.
 */
typedef struct xx_minidump {
    Abstractformat format;
    uint64_t number_of_records;
} xx_minidump;

typedef xx_minidump xx_minidump_t;

XXFC_API void xx_minidump_init(xx_minidump *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_minidump *xx_minidump_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_minidump_destroy(xx_minidump *archive);
XXFC_API void xx_minidump_free(xx_minidump *archive);

XXFC_API bool xx_minidump_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_minidump_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_minidump_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_minidump_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_minidump_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_minidump_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_minidump_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_minidump_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_minidump_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MINIDUMP_H */
