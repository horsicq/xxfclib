/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_unixpack.h @brief Standalone Unix pack stream reader. */

#ifndef XXFCLIB_FORMAT_UNIXPACK_H
#define XXFCLIB_FORMAT_UNIXPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_unixpack {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    bool is_old_version;
} xx_unixpack;

typedef xx_unixpack xx_unixpack_t;
typedef xx_unixpack XUnixPack;

XXFC_API void xx_unixpack_init(xx_unixpack *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_unixpack *xx_unixpack_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_unixpack_destroy(xx_unixpack *archive);
XXFC_API void xx_unixpack_free(xx_unixpack *archive);

XXFC_API bool xx_unixpack_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_unixpack_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_unixpack_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_unixpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the complete Unix pack stream to a caller-provided device. */
XXFC_API bool xx_unixpack_unpack_to_device(xx_unixpack *archive,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_unixpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_unixpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_unixpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_unixpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_unixpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_unixpack_get_uncompressed_size(
    const xx_unixpack *archive);
XXFC_API int64_t xx_unixpack_get_stream_end(const xx_unixpack *archive);
XXFC_API bool xx_unixpack_is_old_version(const xx_unixpack *archive);

static inline Abstractformat *xx_unixpack_to_format(xx_unixpack *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UNIXPACK_H */
