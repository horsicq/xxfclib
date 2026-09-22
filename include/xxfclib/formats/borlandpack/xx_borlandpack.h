/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_borlandpack.h @brief Borland PACK archive reader. */

#ifndef XXFCLIB_FORMAT_BORLANDPACK_H
#define XXFCLIB_FORMAT_BORLANDPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Borland PACK archive: a 36 byte ASCII preamble naming one archive-wide compression method, followed by members that each carry two text header lines and a headerless Unix compress (LZW) payload.
 */
typedef struct xx_borlandpack {
    Abstractformat format;
    uint64_t number_of_records;
} xx_borlandpack;

typedef xx_borlandpack xx_borlandpack_t;

XXFC_API void xx_borlandpack_init(xx_borlandpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_borlandpack *xx_borlandpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_borlandpack_destroy(xx_borlandpack *archive);
XXFC_API void xx_borlandpack_free(xx_borlandpack *archive);

XXFC_API bool xx_borlandpack_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_borlandpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_borlandpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_borlandpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_borlandpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_borlandpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_borlandpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_borlandpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_borlandpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BORLANDPACK_H */
