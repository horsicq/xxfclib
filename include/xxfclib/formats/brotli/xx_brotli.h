/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_brotli.h @brief Standalone Brotli stream reader. */

#ifndef XXFCLIB_FORMAT_BROTLI_H
#define XXFCLIB_FORMAT_BROTLI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_brotli {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_brotli;

typedef xx_brotli xx_brotli_t;
typedef xx_brotli XBrotli;

XXFC_API void xx_brotli_init(xx_brotli *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_brotli *xx_brotli_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_brotli_destroy(xx_brotli *archive);
XXFC_API void xx_brotli_free(xx_brotli *archive);

XXFC_API bool xx_brotli_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_brotli_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_brotli_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_brotli_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the complete Brotli stream to a caller-provided device. */
XXFC_API bool xx_brotli_unpack_to_device(xx_brotli *archive,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_brotli_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_brotli_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_brotli_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_brotli_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_brotli_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_brotli_get_uncompressed_size(const xx_brotli *archive);
XXFC_API int64_t xx_brotli_get_stream_end(const xx_brotli *archive);

static inline Abstractformat *xx_brotli_to_format(xx_brotli *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BROTLI_H */
