/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zz.h @brief ZZ compressed stream reader. */

#ifndef XXFCLIB_FORMAT_ZZ_H
#define XXFCLIB_FORMAT_ZZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZZ container: a 16 byte little-endian header carrying a four byte magic, the decoded length and eight reserved zero bytes, followed by a single zlib stream running to end of file.
 */
typedef struct xx_zz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zz;

typedef xx_zz xx_zz_t;

XXFC_API void xx_zz_init(xx_zz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zz *xx_zz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zz_destroy(xx_zz *archive);
XXFC_API void xx_zz_free(xx_zz *archive);

XXFC_API bool xx_zz_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZZ_H */
