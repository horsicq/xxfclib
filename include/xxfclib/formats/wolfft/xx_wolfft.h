/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wolfft.h @brief An id Software Wolfenstein 3D / Blake Stone VSWAP chunk file: a 6-word header, a table of 32-bit offsets followed by a table of 16-bit lengths, and stored chunks. */

#ifndef XXFCLIB_FORMAT_WOLFFT_H
#define XXFCLIB_FORMAT_WOLFFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An id Software Wolfenstein 3D / Blake Stone VSWAP chunk file: a 6-word header, a table of 32-bit offsets followed by a table of 16-bit lengths, and stored chunks.
 */
typedef struct xx_wolfft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_wolfft;

typedef xx_wolfft xx_wolfft_t;

XXFC_API void xx_wolfft_init(xx_wolfft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_wolfft *xx_wolfft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_wolfft_destroy(xx_wolfft *archive);
XXFC_API void xx_wolfft_free(xx_wolfft *archive);

XXFC_API bool xx_wolfft_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wolfft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_wolfft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_wolfft_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wolfft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wolfft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wolfft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wolfft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wolfft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WOLFFT_H */
