/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_kpck.h @brief KPCK packed-stream reader. */

#ifndef XXFCLIB_FORMAT_KPCK_H
#define XXFCLIB_FORMAT_KPCK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A KPCK file: a 12-byte header carrying the "KPCK" magic, the
 * plaintext length and a method word, followed by a single packed stream
 * running to end-of-file.  It is a one-file compressor, not a container.
 */
typedef struct xx_kpck {
    Abstractformat format;
    uint64_t number_of_records;
} xx_kpck;

typedef xx_kpck xx_kpck_t;

XXFC_API void xx_kpck_init(xx_kpck *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_kpck *xx_kpck_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_kpck_destroy(xx_kpck *archive);
XXFC_API void xx_kpck_free(xx_kpck *archive);

XXFC_API bool xx_kpck_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_kpck_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_kpck_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_kpck_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_kpck_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kpck_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kpck_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kpck_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kpck_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_KPCK_H */
