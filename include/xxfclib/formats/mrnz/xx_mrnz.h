/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mrnz.h @brief MRNZ (PC DOS installer) wrapped file reader. */

#ifndef XXFCLIB_FORMAT_MRNZ_H
#define XXFCLIB_FORMAT_MRNZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An MRNZ file: a twelve byte header carrying the "MRNZ" magic, the
 * SZDD/KWAJ family check word and the count of obfuscated bytes, followed by a
 * payload whose leading bytes are XOR masked and whose remainder is stored
 * verbatim.
 */
typedef struct xx_mrnz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mrnz;

typedef xx_mrnz xx_mrnz_t;

XXFC_API void xx_mrnz_init(xx_mrnz *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_mrnz *xx_mrnz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mrnz_destroy(xx_mrnz *archive);
XXFC_API void xx_mrnz_free(xx_mrnz *archive);

XXFC_API bool xx_mrnz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mrnz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mrnz_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_mrnz_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mrnz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mrnz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mrnz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mrnz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mrnz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MRNZ_H */
