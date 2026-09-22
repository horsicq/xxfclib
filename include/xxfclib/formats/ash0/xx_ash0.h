/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ash0.h @brief Nintendo ASH0 reader. */

#ifndef XXFCLIB_FORMAT_ASH0_H
#define XXFCLIB_FORMAT_ASH0_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ash0 {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t distance_offset;
    unsigned distance_bits;
    uint8_t size_word_top_byte;
} xx_ash0;

typedef xx_ash0 xx_ash0_t;
typedef xx_ash0 XAsh0;

XXFC_API void xx_ash0_init(xx_ash0 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_ash0 *xx_ash0_create(xx_io_device *device,
                                  int64_t base_address);
XXFC_API void xx_ash0_destroy(xx_ash0 *archive);
XXFC_API void xx_ash0_free(xx_ash0 *archive);

XXFC_API bool xx_ash0_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ash0_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_ash0_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_ash0_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ash0_unpack_to_device(xx_ash0 *archive,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ash0_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ash0_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ash0_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ash0_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ash0_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_ash0_get_uncompressed_size(const xx_ash0 *archive);
XXFC_API int64_t xx_ash0_get_stream_end(const xx_ash0 *archive);
XXFC_API uint32_t xx_ash0_get_distance_offset(const xx_ash0 *archive);
XXFC_API unsigned xx_ash0_get_distance_bits(const xx_ash0 *archive);

static inline Abstractformat *xx_ash0_to_format(xx_ash0 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ASH0_H */
