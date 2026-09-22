/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_clp.h @brief Windows Clipboard archive reader. */

#ifndef XXFCLIB_FORMAT_CLP_H
#define XXFCLIB_FORMAT_CLP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Windows clipboard file: a four-byte identifier-and-count header followed by a flat table of 0x59-byte records, each naming a clipboard format and an absolute file offset at which that format's bytes live.
 */
typedef struct xx_clp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_clp;

typedef xx_clp xx_clp_t;

XXFC_API void xx_clp_init(xx_clp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_clp *xx_clp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_clp_destroy(xx_clp *archive);
XXFC_API void xx_clp_free(xx_clp *archive);

XXFC_API bool xx_clp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_clp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_clp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_clp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_clp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_clp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_clp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_clp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_clp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CLP_H */
