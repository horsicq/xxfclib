/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_stylus.h @brief Stylus dictionary reader. */

#ifndef XXFCLIB_FORMAT_STYLUS_H
#define XXFCLIB_FORMAT_STYLUS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Stylus dictionary: a sixteen-byte 'DP' header carrying a container version, a stream kind, an 'SDC' payload tag and a CRC-32 of the plaintext, followed by one XOR-obfuscated LZSS stream that runs to end-of-file.
 */
typedef struct xx_stylus {
    Abstractformat format;
    uint64_t number_of_records;
} xx_stylus;

typedef xx_stylus xx_stylus_t;

XXFC_API void xx_stylus_init(xx_stylus *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_stylus *xx_stylus_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_stylus_destroy(xx_stylus *archive);
XXFC_API void xx_stylus_free(xx_stylus *archive);

XXFC_API bool xx_stylus_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_stylus_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_stylus_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_stylus_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stylus_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stylus_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stylus_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stylus_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stylus_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STYLUS_H */
