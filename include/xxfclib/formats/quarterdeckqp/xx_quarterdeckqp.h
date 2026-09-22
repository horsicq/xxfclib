/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_quarterdeckqp.h @brief Quarterdeck QP package reader. */

#ifndef XXFCLIB_FORMAT_QUARTERDECKQP_H
#define XXFCLIB_FORMAT_QUARTERDECKQP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Quarterdeck QP install package: a 16-byte "QP" header, a fixed-stride index naming every member, then a chain of "QD" records - members and install-path records - whose PKWARE DCL imploded streams tile the rest of the file exactly.
 */
typedef struct xx_quarterdeckqp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_quarterdeckqp;

typedef xx_quarterdeckqp xx_quarterdeckqp_t;

XXFC_API void xx_quarterdeckqp_init(xx_quarterdeckqp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_quarterdeckqp *xx_quarterdeckqp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_quarterdeckqp_destroy(xx_quarterdeckqp *archive);
XXFC_API void xx_quarterdeckqp_free(xx_quarterdeckqp *archive);

XXFC_API bool xx_quarterdeckqp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_quarterdeckqp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_quarterdeckqp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_quarterdeckqp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_quarterdeckqp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_quarterdeckqp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_quarterdeckqp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_quarterdeckqp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_quarterdeckqp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QUARTERDECKQP_H */
