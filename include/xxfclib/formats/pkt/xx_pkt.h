/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pkt.h @brief FidoNet mail packet reader. */

#ifndef XXFCLIB_FORMAT_PKT_H
#define XXFCLIB_FORMAT_PKT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A FidoNet type-2 mail packet: a 0x3a-byte transport header followed by a chain of packed message records, each rendered as one text file.
 */
typedef struct xx_pkt {
    Abstractformat format;
    uint64_t number_of_records;
} xx_pkt;

typedef xx_pkt xx_pkt_t;

XXFC_API void xx_pkt_init(xx_pkt *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_pkt *xx_pkt_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_pkt_destroy(xx_pkt *archive);
XXFC_API void xx_pkt_free(xx_pkt *archive);

XXFC_API bool xx_pkt_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_pkt_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_pkt_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_pkt_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pkt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pkt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pkt_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pkt_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pkt_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PKT_H */
