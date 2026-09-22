/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzip.h @brief Standalone Lzip stream reader. */

#ifndef XXFCLIB_FORMAT_LZIP_H
#define XXFCLIB_FORMAT_LZIP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzip {
    Abstractformat format;
    uint64_t number_of_members;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_lzip;

typedef xx_lzip xx_lzip_t;
typedef xx_lzip XLzip;

XXFC_API void xx_lzip_init(xx_lzip *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_lzip *xx_lzip_create(xx_io_device *device,
                                 int64_t base_address);
XXFC_API void xx_lzip_destroy(xx_lzip *archive);
XXFC_API void xx_lzip_free(xx_lzip *archive);

XXFC_API bool xx_lzip_check_is_valid(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API bool xx_lzip_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_lzip_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lzip_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode every consecutive Lzip member into a caller-provided device. */
XXFC_API bool xx_lzip_unpack_to_device(xx_lzip *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lzip_get_number_of_members(const xx_lzip *archive);
XXFC_API uint64_t xx_lzip_get_uncompressed_size(const xx_lzip *archive);
XXFC_API int64_t xx_lzip_get_stream_end(const xx_lzip *archive);

static inline Abstractformat *xx_lzip_to_format(xx_lzip *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZIP_H */
