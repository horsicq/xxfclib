/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_miz.h @brief MIZ compressed file reader. */

#ifndef XXFCLIB_FORMAT_MIZ_H
#define XXFCLIB_FORMAT_MIZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MIZ container: a 12-byte double-magic header, one stored file name, one 12-byte record and a single PKWARE DCL imploded stream closed by an "MJDK" footer.
 */
typedef struct xx_miz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_miz;

typedef xx_miz xx_miz_t;

XXFC_API void xx_miz_init(xx_miz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_miz *xx_miz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_miz_destroy(xx_miz *archive);
XXFC_API void xx_miz_free(xx_miz *archive);

XXFC_API bool xx_miz_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_miz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_miz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_miz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_miz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_miz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_miz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_miz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_miz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MIZ_H */
