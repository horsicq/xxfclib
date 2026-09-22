/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zap.h @brief ZAP archive reader. */

#ifndef XXFCLIB_FORMAT_ZAP_H
#define XXFCLIB_FORMAT_ZAP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZAP archive: a magic-less chain of 0x15 byte headers, each naming a member in a 12 byte field and followed immediately by a PKWARE DCL implode stream whose decoded length the container does not store.
 */
typedef struct xx_zap {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zap;

typedef xx_zap xx_zap_t;

XXFC_API void xx_zap_init(xx_zap *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zap *xx_zap_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zap_destroy(xx_zap *archive);
XXFC_API void xx_zap_free(xx_zap *archive);

XXFC_API bool xx_zap_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zap_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zap_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zap_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zap_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zap_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zap_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zap_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zap_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZAP_H */
