/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zcmp.h @brief Zcmp compressed file reader. */

#ifndef XXFCLIB_FORMAT_ZCMP_H
#define XXFCLIB_FORMAT_ZCMP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Zcmp compressed file: a 40-byte big-endian header stating the plaintext length and the block size, a seek index that is never read, and then the payload as back-to-back zlib streams, one per block, holding the single unnamed member that fills the rest of the file.
 */
typedef struct xx_zcmp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zcmp;

typedef xx_zcmp xx_zcmp_t;

XXFC_API void xx_zcmp_init(xx_zcmp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zcmp *xx_zcmp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zcmp_destroy(xx_zcmp *archive);
XXFC_API void xx_zcmp_free(xx_zcmp *archive);

XXFC_API bool xx_zcmp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zcmp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zcmp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zcmp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zcmp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zcmp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zcmp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zcmp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zcmp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZCMP_H */
