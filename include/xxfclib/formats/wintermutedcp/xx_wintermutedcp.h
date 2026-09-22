/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wintermutedcp.h @brief A Wintermute Engine DCP package: a 128-byte header, a trailing directory of length-prefixed names, and stored or zlib-deflated payloads. */

#ifndef XXFCLIB_FORMAT_WINTERMUTEDCP_H
#define XXFCLIB_FORMAT_WINTERMUTEDCP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Wintermute Engine DCP package: a 128-byte header, a trailing directory of length-prefixed names, and stored or zlib-deflated payloads.
 */
typedef struct xx_wintermutedcp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_wintermutedcp;

typedef xx_wintermutedcp xx_wintermutedcp_t;

XXFC_API void xx_wintermutedcp_init(xx_wintermutedcp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_wintermutedcp *xx_wintermutedcp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_wintermutedcp_destroy(xx_wintermutedcp *archive);
XXFC_API void xx_wintermutedcp_free(xx_wintermutedcp *archive);

XXFC_API bool xx_wintermutedcp_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wintermutedcp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_wintermutedcp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_wintermutedcp_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wintermutedcp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wintermutedcp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wintermutedcp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wintermutedcp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wintermutedcp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WINTERMUTEDCP_H */
