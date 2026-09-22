/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_qnxbase.h @brief QNX Neutrino boot image reader. */

#ifndef XXFCLIB_FORMAT_QNXBASE_H
#define XXFCLIB_FORMAT_QNXBASE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A QNX Neutrino boot image: IPL boot record, startup code, and a UCL NRV2B compressed image filesystem whose directory is only readable once the whole payload is decompressed.
 */
typedef struct xx_qnxbase {
    Abstractformat format;
    uint64_t number_of_records;
} xx_qnxbase;

typedef xx_qnxbase xx_qnxbase_t;

XXFC_API void xx_qnxbase_init(xx_qnxbase *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_qnxbase *xx_qnxbase_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_qnxbase_destroy(xx_qnxbase *archive);
XXFC_API void xx_qnxbase_free(xx_qnxbase *archive);

XXFC_API bool xx_qnxbase_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_qnxbase_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_qnxbase_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_qnxbase_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_qnxbase_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qnxbase_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qnxbase_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qnxbase_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qnxbase_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QNXBASE_H */
