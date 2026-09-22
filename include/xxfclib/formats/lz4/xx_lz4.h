/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lz4.h @brief Standalone LZ4 frame-stream reader. */

#ifndef XXFCLIB_FORMAT_LZ4_H
#define XXFCLIB_FORMAT_LZ4_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lz4 {
    Abstractformat format;
    uint64_t number_of_frames;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_lz4;

typedef xx_lz4 xx_lz4_t;
typedef xx_lz4 XLz4;

XXFC_API void xx_lz4_init(xx_lz4 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_lz4 *xx_lz4_create(xx_io_device *device,
                                int64_t base_address);
XXFC_API void xx_lz4_destroy(xx_lz4 *archive);
XXFC_API void xx_lz4_free(xx_lz4 *archive);

XXFC_API bool xx_lz4_check_is_valid(Abstractformat *self,
                                     xx_pd_struct *pd);
XXFC_API bool xx_lz4_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_lz4_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_lz4_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode every standard LZ4 frame in the stream to a caller-provided device. */
XXFC_API bool xx_lz4_unpack_to_device(xx_lz4 *archive,
                                      xx_io_device *destination,
                                      xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lz4_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lz4_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lz4_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lz4_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lz4_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lz4_get_number_of_frames(const xx_lz4 *archive);
XXFC_API uint64_t xx_lz4_get_uncompressed_size(const xx_lz4 *archive);
XXFC_API int64_t xx_lz4_get_stream_end(const xx_lz4 *archive);

static inline Abstractformat *xx_lz4_to_format(xx_lz4 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZ4_H */
