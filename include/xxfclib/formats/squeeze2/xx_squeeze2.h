/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_squeeze2.h @brief Squeeze II (0xFFFA) single-file squeezed container. */

#ifndef XXFCLIB_FORMAT_SQUEEZE2_H
#define XXFCLIB_FORMAT_SQUEEZE2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Squeeze II: the Greenlaw squeeze codec behind a richer header that adds
 * a date string, a DOS timestamp and the CP/M end-of-text byte. */
typedef struct xx_squeeze2 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_squeeze2;

typedef struct xx_squeeze2 xx_squeeze2_t;

XXFC_API void xx_squeeze2_init(xx_squeeze2 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_squeeze2 *xx_squeeze2_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_squeeze2_destroy(xx_squeeze2 *archive);
XXFC_API void xx_squeeze2_free(xx_squeeze2 *archive);
XXFC_API bool xx_squeeze2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_squeeze2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_squeeze2_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_squeeze2_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_squeeze2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_squeeze2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_squeeze2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_squeeze2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_squeeze2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_squeeze2_to_format(xx_squeeze2 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQUEEZE2_H */
