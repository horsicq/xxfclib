/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rompaq.h @brief Compaq ROMPAQ firmware image reader. */

#ifndef XXFCLIB_FORMAT_ROMPAQ_H
#define XXFCLIB_FORMAT_ROMPAQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Compaq ROMPAQ firmware image: one 0x48-byte header describing a single ROM image held either stored, as one PKWARE DCL stream, or as a chain of independently imploded banks.
 */
typedef struct xx_rompaq {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rompaq;

typedef xx_rompaq xx_rompaq_t;

XXFC_API void xx_rompaq_init(xx_rompaq *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rompaq *xx_rompaq_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rompaq_destroy(xx_rompaq *archive);
XXFC_API void xx_rompaq_free(xx_rompaq *archive);

XXFC_API bool xx_rompaq_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_rompaq_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rompaq_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rompaq_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rompaq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rompaq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rompaq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rompaq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rompaq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ROMPAQ_H */
