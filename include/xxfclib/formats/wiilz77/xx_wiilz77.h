/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wiilz77.h @brief Nintendo LZ10/LZ11 archive-style reader. */

#ifndef XXFCLIB_FORMAT_WIILZ77_H
#define XXFCLIB_FORMAT_WIILZ77_H

#include "xxfclib/algo/wiilz77/xx_wiilz77.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_wiilz77 {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t data_offset;
    uint32_t imd5_declared_size;
    xx_wiilz77_variant_t variant;
    bool has_tag;
    bool has_imd5_wrapper;
} xx_wiilz77;

typedef xx_wiilz77 xx_wiilz77_t;
typedef xx_wiilz77 XWiiLZ77;

XXFC_API void xx_wiilz77_init(xx_wiilz77 *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_wiilz77 *xx_wiilz77_create(xx_io_device *device,
                                        int64_t base_address);
XXFC_API void xx_wiilz77_destroy(xx_wiilz77 *archive);
XXFC_API void xx_wiilz77_free(xx_wiilz77 *archive);

XXFC_API bool xx_wiilz77_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_wiilz77_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_wiilz77_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_wiilz77_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wiilz77_unpack_to_device(xx_wiilz77 *archive,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wiilz77_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wiilz77_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wiilz77_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wiilz77_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wiilz77_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_wiilz77_get_uncompressed_size(
    const xx_wiilz77 *archive);
XXFC_API int64_t xx_wiilz77_get_stream_end(const xx_wiilz77 *archive);
XXFC_API xx_wiilz77_variant_t xx_wiilz77_get_variant(
    const xx_wiilz77 *archive);
XXFC_API bool xx_wiilz77_has_tag(const xx_wiilz77 *archive);
XXFC_API bool xx_wiilz77_has_imd5_wrapper(const xx_wiilz77 *archive);

static inline Abstractformat *xx_wiilz77_to_format(xx_wiilz77 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WIILZ77_H */
