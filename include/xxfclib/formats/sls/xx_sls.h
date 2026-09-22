/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sls.h @brief WinSense SLS compressed-file reader. */

#ifndef XXFCLIB_FORMAT_SLS_H
#define XXFCLIB_FORMAT_SLS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WinSense SLS container: a 13-byte header (9-byte magic plus the
 * plaintext size) followed by one LZHUF-compressed member that runs to the
 * end of the file.  Exactly one member, and no name is stored for it.
 */
typedef struct xx_sls {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t uncompressed_size; /**< the header's stored plaintext size, -1 if unknown */
} xx_sls;

typedef xx_sls xx_sls_t;

XXFC_API void xx_sls_init(xx_sls *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_sls *xx_sls_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sls_destroy(xx_sls *archive);
XXFC_API void xx_sls_free(xx_sls *archive);

XXFC_API bool xx_sls_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sls_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sls_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_sls_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sls_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sls_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sls_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sls_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sls_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SLS_H */
