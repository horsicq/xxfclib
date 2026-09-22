/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_edilzss.h @brief EDI Install LZSS packed file reader. */

#ifndef XXFCLIB_FORMAT_EDILZSS_H
#define XXFCLIB_FORMAT_EDILZSS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An EDI Install packed file: the signature "EDILZSS" plus a version
 * digit, an optional 13-byte original file name, a plaintext length in
 * version 2 only, and one Okumura-style LZSS stream running to end of file.
 */
typedef struct xx_edilzss {
    Abstractformat format;
    uint64_t number_of_records;
} xx_edilzss;

typedef xx_edilzss xx_edilzss_t;

XXFC_API void xx_edilzss_init(xx_edilzss *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_edilzss *xx_edilzss_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_edilzss_destroy(xx_edilzss *archive);
XXFC_API void xx_edilzss_free(xx_edilzss *archive);

XXFC_API bool xx_edilzss_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_edilzss_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_edilzss_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_edilzss_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_edilzss_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_edilzss_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_edilzss_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_edilzss_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_edilzss_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EDILZSS_H */
