/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nintendo_u8.h @brief Nintendo U8 archive reader. */

#ifndef XXFCLIB_FORMAT_NINTENDO_U8_H
#define XXFCLIB_FORMAT_NINTENDO_U8_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Nintendo U8 container. */
typedef struct xx_nintendo_u8 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_nintendo_u8;

typedef xx_nintendo_u8 xx_nintendo_u8_t;

XXFC_API void xx_nintendo_u8_init(xx_nintendo_u8 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_nintendo_u8 *xx_nintendo_u8_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_nintendo_u8_destroy(xx_nintendo_u8 *archive);
XXFC_API void xx_nintendo_u8_free(xx_nintendo_u8 *archive);

XXFC_API bool xx_nintendo_u8_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_nintendo_u8_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_nintendo_u8_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_nintendo_u8_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nintendo_u8_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nintendo_u8_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nintendo_u8_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nintendo_u8_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nintendo_u8_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NINTENDO_U8_H */
