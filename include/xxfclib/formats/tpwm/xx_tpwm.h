/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_tpwm.h @brief Turbo Packer (TPWM) packed file reader. */

#ifndef XXFCLIB_FORMAT_TPWM_H
#define XXFCLIB_FORMAT_TPWM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Turbo Packer file: an eight byte header carrying the "TPWM" magic
 * and the big endian plaintext length, followed by a single bit tagged LZ77
 * stream running to end-of-file.
 */
typedef struct xx_tpwm {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tpwm;

typedef xx_tpwm xx_tpwm_t;

XXFC_API void xx_tpwm_init(xx_tpwm *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_tpwm *xx_tpwm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_tpwm_destroy(xx_tpwm *archive);
XXFC_API void xx_tpwm_free(xx_tpwm *archive);

XXFC_API bool xx_tpwm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tpwm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_tpwm_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_tpwm_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tpwm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tpwm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tpwm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tpwm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tpwm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TPWM_H */
