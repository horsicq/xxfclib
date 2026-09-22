/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_beatthehouse.h @brief Beat The House "PAK?" packed-stream reader. */

#ifndef XXFCLIB_FORMAT_BEATTHEHOUSE_H
#define XXFCLIB_FORMAT_BEATTHEHOUSE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Beat The House packed file: an 8-byte header carrying the "PAK"
 * magic plus a one-letter payload-kind tag and the plaintext length, followed
 * by a single packed stream running to end-of-file.  It is a one-file
 * compressor, not a container.
 */
typedef struct xx_beatthehouse {
    Abstractformat format;
    uint64_t number_of_records;
} xx_beatthehouse;

typedef xx_beatthehouse xx_beatthehouse_t;

XXFC_API void xx_beatthehouse_init(xx_beatthehouse *archive,
                                   xx_io_device *device, int64_t base_address);
XXFC_API xx_beatthehouse *xx_beatthehouse_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_beatthehouse_destroy(xx_beatthehouse *archive);
XXFC_API void xx_beatthehouse_free(xx_beatthehouse *archive);

XXFC_API bool xx_beatthehouse_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_beatthehouse_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_beatthehouse_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_beatthehouse_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_beatthehouse_create_archive_records_reading(Abstractformat *self,
                                               const xx_list_s *options,
                                               xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_beatthehouse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_beatthehouse_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_beatthehouse_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_beatthehouse_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BEATTHEHOUSE_H */
