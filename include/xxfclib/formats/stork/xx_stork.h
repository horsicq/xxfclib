/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_stork.h @brief Stork install archive reader. */

#ifndef XXFCLIB_FORMAT_STORK_H
#define XXFCLIB_FORMAT_STORK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Stork DOS install archive: 0x12-byte member records, each followed by its PKWARE DCL imploded stream, tiling the file exactly from offset 0 to EOF, the first member always being the runtime's own @ASSOC.SAV association table.
 */
typedef struct xx_stork {
    Abstractformat format;
    uint64_t number_of_records;
} xx_stork;

typedef xx_stork xx_stork_t;

XXFC_API void xx_stork_init(xx_stork *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_stork *xx_stork_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_stork_destroy(xx_stork *archive);
XXFC_API void xx_stork_free(xx_stork *archive);

XXFC_API bool xx_stork_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_stork_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_stork_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_stork_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stork_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stork_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stork_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stork_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stork_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STORK_H */
