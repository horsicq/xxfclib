/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lz5.h @brief Standalone LZ5 frame-stream reader. */

#ifndef XXFCLIB_FORMAT_LZ5_H
#define XXFCLIB_FORMAT_LZ5_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lz5 {
    Abstractformat format;
    uint64_t number_of_frames;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_lz5;

typedef xx_lz5 xx_lz5_t;
typedef xx_lz5 XLz5;

XXFC_API void xx_lz5_init(xx_lz5 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_lz5 *xx_lz5_create(xx_io_device *device,
                                int64_t base_address);
XXFC_API void xx_lz5_destroy(xx_lz5 *archive);
XXFC_API void xx_lz5_free(xx_lz5 *archive);

XXFC_API bool xx_lz5_check_is_valid(Abstractformat *self,
                                     xx_pd_struct *pd);
XXFC_API bool xx_lz5_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_lz5_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_lz5_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode every standard LZ5 frame in the stream to a caller-provided device. */
XXFC_API bool xx_lz5_unpack_to_device(xx_lz5 *archive,
                                      xx_io_device *destination,
                                      xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lz5_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lz5_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lz5_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lz5_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lz5_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lz5_get_number_of_frames(const xx_lz5 *archive);
XXFC_API uint64_t xx_lz5_get_uncompressed_size(const xx_lz5 *archive);
XXFC_API int64_t xx_lz5_get_stream_end(const xx_lz5 *archive);

static inline Abstractformat *xx_lz5_to_format(xx_lz5 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZ5_H */
