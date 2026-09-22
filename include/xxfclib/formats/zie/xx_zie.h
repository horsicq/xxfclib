/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zie.h @brief ZIE encrypted ZIP reader. */

#ifndef XXFCLIB_FORMAT_ZIE_H
#define XXFCLIB_FORMAT_ZIE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZIE file: the OS/2 'ProtectIt/2' wrapper around an ordinary ZIP, a 0x118-byte header carrying an obfuscated key and the original file name, followed by the ZIP under a 16-byte repeating XOR whose phase is derived from the payload length.
 */
typedef struct xx_zie {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zie;

typedef xx_zie xx_zie_t;

XXFC_API void xx_zie_init(xx_zie *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zie *xx_zie_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zie_destroy(xx_zie *archive);
XXFC_API void xx_zie_free(xx_zie *archive);

XXFC_API bool xx_zie_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zie_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zie_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zie_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zie_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zie_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zie_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zie_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zie_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZIE_H */
