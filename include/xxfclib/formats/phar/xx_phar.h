/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_phar.h @brief PHP PHAR archive. */

#ifndef XXFCLIB_FORMAT_PHAR_H
#define XXFCLIB_FORMAT_PHAR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A PHP PHAR: a PHP stub ending in __HALT_COMPILER(), then a manifest
 * describing every member, then the member bodies in that order. */
typedef struct xx_phar {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_phar;

typedef struct xx_phar xx_phar_t;

XXFC_API void xx_phar_init(xx_phar *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_phar *xx_phar_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_phar_destroy(xx_phar *archive);
XXFC_API void xx_phar_free(xx_phar *archive);
XXFC_API bool xx_phar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_phar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_phar_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_phar_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_phar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_phar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_phar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_phar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_phar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_phar_to_format(xx_phar *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PHAR_H */
