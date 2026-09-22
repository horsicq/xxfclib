/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tarx2.h @brief QNX TARX version 2 reader. */

#ifndef XXFCLIB_FORMAT_TARX2_H
#define XXFCLIB_FORMAT_TARX2_H

#include "xxfclib/formats/tar/xx_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tarx2 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    int64_t encrypted_size;
    int64_t uncompressed_size;
    void *internal;
} xx_tarx2;

typedef xx_tarx2 xx_tarx2_t;
typedef xx_tarx2 XTarx2;

XXFC_API void xx_tarx2_init(xx_tarx2 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_tarx2 *xx_tarx2_create(xx_io_device *device,
                                    int64_t base_address);
XXFC_API void xx_tarx2_destroy(xx_tarx2 *archive);
XXFC_API void xx_tarx2_free(xx_tarx2 *archive);

XXFC_API bool xx_tarx2_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_tarx2_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_tarx2_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_tarx2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_tarx2_create_archive_records_reading(Abstractformat *self,
                                        const xx_list_s *options,
                                        xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tarx2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tarx2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tarx2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tarx2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_tarx2_get_number_of_records(const xx_tarx2 *archive);
XXFC_API uint64_t xx_tarx2_get_number_of_members(const xx_tarx2 *archive);
XXFC_API int64_t xx_tarx2_get_encrypted_size(const xx_tarx2 *archive);
XXFC_API int64_t xx_tarx2_get_uncompressed_size(const xx_tarx2 *archive);

static inline Abstractformat *xx_tarx2_to_format(xx_tarx2 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TARX2_H */
