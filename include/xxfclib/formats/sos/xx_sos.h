/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sos.h @brief SOS bootable Amiga disk reader. */

#ifndef XXFCLIB_FORMAT_SOS_H
#define XXFCLIB_FORMAT_SOS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A SOS bootable Amiga disk image: a 68000 bootblock followed by a table of fixed 32-byte directory records naming stored members.
 */
typedef struct xx_sos {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sos;

typedef xx_sos xx_sos_t;

XXFC_API void xx_sos_init(xx_sos *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sos *xx_sos_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sos_destroy(xx_sos *archive);
XXFC_API void xx_sos_free(xx_sos *archive);

XXFC_API bool xx_sos_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_sos_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sos_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sos_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SOS_H */
