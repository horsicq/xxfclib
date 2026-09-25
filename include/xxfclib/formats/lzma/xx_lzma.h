/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_lzma.h
 * @brief LZMA-alone (.lzma) reader: one "payload" record.
 *
 * Accepts known-size streams with or without an end marker (LZMA SDK
 * `lzma e`), unknown-size streams ended by the marker (xz --format=lzma,
 * liblzma FORMAT_ALONE), and consecutive streams, which decode as one
 * payload the way 7-Zip does.  format_size is the exact extent of the
 * streams; anything after them is reported as overlay.
 */

#ifndef XXFCLIB_FORMAT_LZMA_H
#define XXFCLIB_FORMAT_LZMA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzma {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_lzma;

typedef xx_lzma xx_lzma_t;
typedef xx_lzma XLzma;

XXFC_API void xx_lzma_init(xx_lzma *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_lzma *xx_lzma_create(xx_io_device *device,
                                 int64_t base_address);
XXFC_API void xx_lzma_destroy(xx_lzma *archive);
XXFC_API void xx_lzma_free(xx_lzma *archive);

XXFC_API bool xx_lzma_check_is_valid(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API bool xx_lzma_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_lzma_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lzma_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the payload (every stream measured by handle_base_info, in
 *  order) to a caller-provided device. */
XXFC_API bool xx_lzma_unpack_to_device(xx_lzma *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzma_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzma_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzma_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzma_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzma_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Payload size (all streams), valid after handle_base_info. */
XXFC_API uint64_t xx_lzma_get_uncompressed_size(const xx_lzma *archive);
/** Device offset just past the last stream, or -1. */
XXFC_API int64_t xx_lzma_get_stream_end(const xx_lzma *archive);

static inline Abstractformat *xx_lzma_to_format(xx_lzma *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZMA_H */
