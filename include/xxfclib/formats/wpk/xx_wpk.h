/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wpk.h @brief WPK archive reader. */

#ifndef XXFCLIB_FORMAT_WPK_H
#define XXFCLIB_FORMAT_WPK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WPK archive, the Watcom installer's "pack" container: a 12-byte header, member payloads, and a trailing directory that closes the file.
 */
typedef struct xx_wpk {
    Abstractformat format;
    uint64_t number_of_records;
    /* Memo of the archive-wide sorter probe. It is a pure
     * function of the device bytes, so caching it changes
     * no result - it only stops every extraction from
     * paying for the same trial decodes. */
    uint32_t sorter;
    bool sorter_resolved;
} xx_wpk;

typedef xx_wpk xx_wpk_t;

XXFC_API void xx_wpk_init(xx_wpk *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_wpk *xx_wpk_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_wpk_destroy(xx_wpk *archive);
XXFC_API void xx_wpk_free(xx_wpk *archive);

XXFC_API bool xx_wpk_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_wpk_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_wpk_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_wpk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wpk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wpk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wpk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WPK_H */
