/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_igf2.h @brief IGF installer container reader. */

#ifndef XXFCLIB_FORMAT_IGF2_H
#define XXFCLIB_FORMAT_IGF2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IGF installer container: a 0x28-byte header pointing at a chain of 0x38-byte member records, each followed by its own name and LHA -lh4- stream, ending on a 0xFFFF terminator.
 */
typedef struct xx_igf2 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_igf2;

typedef xx_igf2 xx_igf2_t;

XXFC_API void xx_igf2_init(xx_igf2 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_igf2 *xx_igf2_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_igf2_destroy(xx_igf2 *archive);
XXFC_API void xx_igf2_free(xx_igf2 *archive);

XXFC_API bool xx_igf2_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_igf2_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_igf2_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_igf2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_igf2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_igf2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_igf2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_igf2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_igf2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IGF2_H */
