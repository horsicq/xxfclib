/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_copydisk.h @brief COPYDISK flat disk image. */

#ifndef XXFCLIB_FORMAT_COPYDISK_H
#define XXFCLIB_FORMAT_COPYDISK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A COPYDISK image: a 0x20-byte header and then the raw sector image. */
typedef struct xx_copydisk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_copydisk;

typedef struct xx_copydisk xx_copydisk_t;

XXFC_API void xx_copydisk_init(xx_copydisk *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_copydisk *xx_copydisk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_copydisk_destroy(xx_copydisk *archive);
XXFC_API void xx_copydisk_free(xx_copydisk *archive);
XXFC_API bool xx_copydisk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_copydisk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_copydisk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_copydisk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_copydisk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_copydisk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_copydisk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_copydisk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_copydisk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_copydisk_to_format(xx_copydisk *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_COPYDISK_H */
