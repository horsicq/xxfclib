/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_unixcompress.h @brief Standalone Unix compress (.Z) reader. */

#ifndef XXFCLIB_FORMAT_UNIXCOMPRESS_H
#define XXFCLIB_FORMAT_UNIXCOMPRESS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_unixcompress {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
} xx_unixcompress;

typedef xx_unixcompress xx_unixcompress_t;
typedef xx_unixcompress XUnixCompress;

XXFC_API void xx_unixcompress_init(xx_unixcompress *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_unixcompress *xx_unixcompress_create(xx_io_device *device,
                                                  int64_t base_address);
XXFC_API void xx_unixcompress_destroy(xx_unixcompress *archive);
XXFC_API void xx_unixcompress_free(xx_unixcompress *archive);

XXFC_API bool xx_unixcompress_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_unixcompress_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_unixcompress_get_format_size(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API uint64_t xx_unixcompress_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the complete .Z stream to a caller-provided device. */
XXFC_API bool xx_unixcompress_unpack_to_device(xx_unixcompress *archive,
                                                xx_io_device *destination,
                                                xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_unixcompress_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_unixcompress_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_unixcompress_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_unixcompress_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_unixcompress_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_unixcompress_get_uncompressed_size(
    const xx_unixcompress *archive);
XXFC_API int64_t xx_unixcompress_get_stream_end(const xx_unixcompress *archive);

static inline Abstractformat *xx_unixcompress_to_format(
    xx_unixcompress *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UNIXCOMPRESS_H */
