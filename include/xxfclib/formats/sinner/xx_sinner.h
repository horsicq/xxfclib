/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sinner.h @brief CCT "|CCTfs2.0|" resource container reader. */

#ifndef XXFCLIB_FORMAT_SINNER_H
#define XXFCLIB_FORMAT_SINNER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CCT filesystem 2.0 resource container: a 24-byte header, a table
 * of 264-byte (256-byte path, size, relative offset) entries and contiguous
 * stored payloads.
 */
typedef struct xx_sinner {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sinner;

typedef xx_sinner xx_sinner_t;

XXFC_API void xx_sinner_init(xx_sinner *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_sinner *xx_sinner_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sinner_destroy(xx_sinner *archive);
XXFC_API void xx_sinner_free(xx_sinner *archive);

XXFC_API bool xx_sinner_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sinner_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sinner_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_sinner_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sinner_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sinner_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sinner_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sinner_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sinner_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SINNER_H */
