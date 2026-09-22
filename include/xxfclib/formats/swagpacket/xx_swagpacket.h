/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_swagpacket.h @brief SWAG packet reader. */

#ifndef XXFCLIB_FORMAT_SWAGPACKET_H
#define XXFCLIB_FORMAT_SWAGPACKET_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SWAG packet: a 128-byte banner block followed by a chain of 128-byte member headers, each carrying its stored Pascal snippet in the blocks that follow it.
 */
typedef struct xx_swagpacket {
    Abstractformat format;
    uint64_t number_of_records;
} xx_swagpacket;

typedef xx_swagpacket xx_swagpacket_t;

XXFC_API void xx_swagpacket_init(xx_swagpacket *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_swagpacket *xx_swagpacket_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_swagpacket_destroy(xx_swagpacket *archive);
XXFC_API void xx_swagpacket_free(xx_swagpacket *archive);

XXFC_API bool xx_swagpacket_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_swagpacket_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_swagpacket_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_swagpacket_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_swagpacket_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_swagpacket_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_swagpacket_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_swagpacket_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_swagpacket_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SWAGPACKET_H */
