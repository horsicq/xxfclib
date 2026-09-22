/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_netwarepacked.h @brief NetWare packed file reader. */

#ifndef XXFCLIB_FORMAT_NETWAREPACKED_H
#define XXFCLIB_FORMAT_NETWAREPACKED_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Personal NetWare / Novell DOS packed file: a 31-byte header carrying the literal 'Packed File ' tag, the original name, a version/method pair and the plaintext length, followed by one LSB-first token stream running to end-of-file.
 */
typedef struct xx_netwarepacked {
    Abstractformat format;
    uint64_t number_of_records;
} xx_netwarepacked;

typedef xx_netwarepacked xx_netwarepacked_t;

XXFC_API void xx_netwarepacked_init(xx_netwarepacked *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_netwarepacked *xx_netwarepacked_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_netwarepacked_destroy(xx_netwarepacked *archive);
XXFC_API void xx_netwarepacked_free(xx_netwarepacked *archive);

XXFC_API bool xx_netwarepacked_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_netwarepacked_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_netwarepacked_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_netwarepacked_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_netwarepacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_netwarepacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_netwarepacked_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_netwarepacked_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_netwarepacked_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NETWAREPACKED_H */
