/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_claylz.h @brief Clay (*.cmz) archive reader. */

#ifndef XXFCLIB_FORMAT_CLAYLZ_H
#define XXFCLIB_FORMAT_CLAYLZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Clay archive: a bare chain of members, each a 20-byte header, a
 *        fixed-length name field and the member's Clay LZ stream. There is no
 *        central directory, no member count and no terminator -- the chain
 *        ends where the next 'Clay' magic fails to appear.
 */
typedef struct xx_claylz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_claylz;

typedef xx_claylz xx_claylz_t;

XXFC_API void xx_claylz_init(xx_claylz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_claylz *xx_claylz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_claylz_destroy(xx_claylz *archive);
XXFC_API void xx_claylz_free(xx_claylz *archive);

XXFC_API bool xx_claylz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_claylz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_claylz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_claylz_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_claylz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_claylz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_claylz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_claylz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_claylz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CLAYLZ_H */
