/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_lzop.h @brief LZOP-compressed TAR reader. */

#ifndef XXFCLIB_FORMAT_TAR_LZOP_H
#define XXFCLIB_FORMAT_TAR_LZOP_H

#include "xxfclib/formats/tar/xx_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar_lzop {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t lzop_stream_count;
    int64_t compressed_size;
    int64_t uncompressed_size;
    void *internal;
} xx_tar_lzop;

typedef xx_tar_lzop xx_tar_lzop_t;
typedef xx_tar_lzop XTarLzop;

XXFC_API void xx_tar_lzop_init(xx_tar_lzop *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_tar_lzop *xx_tar_lzop_create(xx_io_device *device,
                                          int64_t base_address);
XXFC_API void xx_tar_lzop_destroy(xx_tar_lzop *archive);
XXFC_API void xx_tar_lzop_free(xx_tar_lzop *archive);

XXFC_API bool xx_tar_lzop_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_tar_lzop_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_tar_lzop_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_lzop_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_tar_lzop_create_archive_records_reading(Abstractformat *self,
                                           const xx_list_s *options,
                                           xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_tar_lzop_get_current_archive_record(Abstractformat *self,
                                        xx_archive_record_state *state);
XXFC_API bool xx_tar_lzop_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_lzop_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_lzop_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_tar_lzop_get_number_of_records(
    const xx_tar_lzop *archive);
XXFC_API uint64_t xx_tar_lzop_get_number_of_members(
    const xx_tar_lzop *archive);
XXFC_API uint64_t xx_tar_lzop_get_lzop_stream_count(
    const xx_tar_lzop *archive);
XXFC_API int64_t xx_tar_lzop_get_compressed_size(
    const xx_tar_lzop *archive);
XXFC_API int64_t xx_tar_lzop_get_uncompressed_size(
    const xx_tar_lzop *archive);

static inline Abstractformat *xx_tar_lzop_to_format(xx_tar_lzop *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_LZOP_H */
