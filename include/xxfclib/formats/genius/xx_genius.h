/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_genius.h @brief Genius Library archive reader. */

#ifndef XXFCLIB_FORMAT_GENIUS_H
#define XXFCLIB_FORMAT_GENIUS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A "GENIUS LIBRARY" (.gpl) container: a 0x20-byte header giving the member count, followed by a chain in which each member's 0x36-byte record and counted name sit directly in front of that member's own data, so the records are found by walking the file rather than from a central directory.
 */
typedef struct xx_genius {
    Abstractformat format;
    uint64_t number_of_records;
} xx_genius;

typedef xx_genius xx_genius_t;

XXFC_API void xx_genius_init(xx_genius *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_genius *xx_genius_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_genius_destroy(xx_genius *archive);
XXFC_API void xx_genius_free(xx_genius *archive);

XXFC_API bool xx_genius_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_genius_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_genius_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_genius_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_genius_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_genius_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_genius_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_genius_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_genius_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GENIUS_H */
