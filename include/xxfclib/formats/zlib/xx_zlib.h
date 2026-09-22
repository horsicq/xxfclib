/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_zlib.h @brief Standalone RFC 1950 zlib stream reader. */

#ifndef XXFCLIB_FORMAT_ZLIB_H
#define XXFCLIB_FORMAT_ZLIB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_zlib {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t adler32;
} xx_zlib;

typedef xx_zlib xx_zlib_t;
typedef xx_zlib XZlib;

XXFC_API void xx_zlib_init(xx_zlib *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_zlib *xx_zlib_create(xx_io_device *device,
                                 int64_t base_address);
XXFC_API void xx_zlib_destroy(xx_zlib *archive);
XXFC_API void xx_zlib_free(xx_zlib *archive);

XXFC_API bool xx_zlib_check_is_valid(Abstractformat *self,
                                     xx_pd_struct *pd);
XXFC_API bool xx_zlib_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_zlib_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_zlib_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the zlib member to a caller-provided device after authenticating
 * its Adler-32 trailer. */
XXFC_API bool xx_zlib_unpack_to_device(xx_zlib *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zlib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zlib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zlib_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zlib_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zlib_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_zlib_get_uncompressed_size(const xx_zlib *archive);
XXFC_API int64_t xx_zlib_get_stream_end(const xx_zlib *archive);
XXFC_API uint32_t xx_zlib_get_adler32(const xx_zlib *archive);

static inline Abstractformat *xx_zlib_to_format(xx_zlib *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZLIB_H */
