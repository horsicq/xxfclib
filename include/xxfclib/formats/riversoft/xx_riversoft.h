/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_riversoft.h @brief RiverSoft Data Library reader. */

#ifndef XXFCLIB_FORMAT_RIVERSOFT_H
#define XXFCLIB_FORMAT_RIVERSOFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A RiverSoft Data Library: a 32-byte text-magic header, the member payloads, and a fixed-width directory of 21-byte records that ends exactly at end of file.
 */
typedef struct xx_riversoft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_riversoft;

typedef xx_riversoft xx_riversoft_t;

XXFC_API void xx_riversoft_init(xx_riversoft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_riversoft *xx_riversoft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_riversoft_destroy(xx_riversoft *archive);
XXFC_API void xx_riversoft_free(xx_riversoft *archive);

XXFC_API bool xx_riversoft_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_riversoft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_riversoft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_riversoft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_riversoft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_riversoft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_riversoft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_riversoft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_riversoft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RIVERSOFT_H */
