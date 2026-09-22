/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzv1.h @brief LZV1 single-file reader. */

#ifndef XXFCLIB_FORMAT_LZV1_H
#define XXFCLIB_FORMAT_LZV1_H

#include "xxfclib/algo/lzv1/xx_lzv1.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzv1 {
    Abstractformat format;
    int64_t stream_end;
    uint16_t max_codes;
} xx_lzv1;

typedef xx_lzv1 xx_lzv1_t;
typedef xx_lzv1 XLzv1;

XXFC_API void xx_lzv1_init(xx_lzv1 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_lzv1 *xx_lzv1_create(xx_io_device *device,
                                  int64_t base_address);
XXFC_API void xx_lzv1_destroy(xx_lzv1 *archive);
XXFC_API void xx_lzv1_free(xx_lzv1 *archive);

XXFC_API bool xx_lzv1_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzv1_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_lzv1_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lzv1_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzv1_unpack_to_device(xx_lzv1 *archive,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzv1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzv1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzv1_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzv1_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzv1_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_lzv1_get_stream_end(const xx_lzv1 *archive);
XXFC_API uint16_t xx_lzv1_get_max_codes(const xx_lzv1 *archive);

static inline Abstractformat *xx_lzv1_to_format(xx_lzv1 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZV1_H */
