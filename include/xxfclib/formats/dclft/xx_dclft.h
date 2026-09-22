/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dclft.h @brief Raw PKWARE DCL (implode) stream reader. */

#ifndef XXFCLIB_FORMAT_DCLFT_H
#define XXFCLIB_FORMAT_DCLFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A bare PKWARE Data Compression Library stream: two prelude bytes and
 * the coded data, with no container, no name, no length and no checksum. The
 * only test that means anything is that the stream decodes to its end marker
 * exactly at end of file.
 */
typedef struct xx_dclft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_dclft;

typedef xx_dclft xx_dclft_t;

XXFC_API void xx_dclft_init(xx_dclft *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_dclft *xx_dclft_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dclft_destroy(xx_dclft *archive);
XXFC_API void xx_dclft_free(xx_dclft *archive);

XXFC_API bool xx_dclft_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dclft_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_dclft_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_dclft_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dclft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dclft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dclft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dclft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dclft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DCLFT_H */
