/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_settlersft.h @brief A Blue Byte Settlers (Funatics) ".pa" resource file: an 8-byte header whose first field is the file's own size, followed by a table of (size, offset) slots. */

#ifndef XXFCLIB_FORMAT_SETTLERSFT_H
#define XXFCLIB_FORMAT_SETTLERSFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Blue Byte Settlers (Funatics) ".pa" resource file: an 8-byte header whose first field is the file's own size, followed by a table of (size, offset) slots.
 */
typedef struct xx_settlersft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_settlersft;

typedef xx_settlersft xx_settlersft_t;

XXFC_API void xx_settlersft_init(xx_settlersft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_settlersft *xx_settlersft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_settlersft_destroy(xx_settlersft *archive);
XXFC_API void xx_settlersft_free(xx_settlersft *archive);

XXFC_API bool xx_settlersft_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_settlersft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_settlersft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_settlersft_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_settlersft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_settlersft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_settlersft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_settlersft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_settlersft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SETTLERSFT_H */
