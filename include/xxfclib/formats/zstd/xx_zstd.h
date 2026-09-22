/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_zstd.h @brief Standalone Zstandard stream reader. */

#ifndef XXFCLIB_FORMAT_ZSTD_H
#define XXFCLIB_FORMAT_ZSTD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_zstd {
    Abstractformat format;
    uint64_t number_of_frames;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_zstd;

typedef xx_zstd xx_zstd_t;
typedef xx_zstd XZstd;

XXFC_API void xx_zstd_init(xx_zstd *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_zstd *xx_zstd_create(xx_io_device *device,
                                 int64_t base_address);
XXFC_API void xx_zstd_destroy(xx_zstd *archive);
XXFC_API void xx_zstd_free(xx_zstd *archive);

XXFC_API bool xx_zstd_check_is_valid(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API bool xx_zstd_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_zstd_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_zstd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode every standard frame in the stream to a caller-provided device. */
XXFC_API bool xx_zstd_unpack_to_device(xx_zstd *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zstd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zstd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zstd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zstd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zstd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_zstd_get_number_of_frames(const xx_zstd *archive);
XXFC_API uint64_t xx_zstd_get_uncompressed_size(const xx_zstd *archive);
XXFC_API int64_t xx_zstd_get_stream_end(const xx_zstd *archive);

static inline Abstractformat *xx_zstd_to_format(xx_zstd *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZSTD_H */
