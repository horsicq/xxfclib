/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_netware2.h @brief NetWare installation-disk file container. */

#ifndef XXFCLIB_FORMAT_NETWARE2_H
#define XXFCLIB_FORMAT_NETWARE2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A NetWare installation-disk file: a chain of length-prefixed named
 * records ("NetWareFileInfo", "NetWareFile", optional "VeRsIoN="/"CoPyRiGhT="
 * strings and "PackedData") that wraps exactly one Novell "Packed File"
 * 01/0A stream together with the original DOS name, date and size.
 */
typedef struct xx_netware2 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_netware2;

typedef xx_netware2 xx_netware2_t;

XXFC_API void xx_netware2_init(xx_netware2 *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_netware2 *xx_netware2_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_netware2_destroy(xx_netware2 *archive);
XXFC_API void xx_netware2_free(xx_netware2 *archive);

XXFC_API bool xx_netware2_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_netware2_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_netware2_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_netware2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_netware2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_netware2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_netware2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_netware2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_netware2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NETWARE2_H */
