/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ea.h @brief Electronic Arts DOS archive reader. */

#ifndef XXFCLIB_FORMAT_EA_H
#define XXFCLIB_FORMAT_EA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Electronic Arts DOS archive: a chain of 48-byte member headers, each immediately followed by its payload, with no archive header and no terminator -- the chain is accepted only when it lands exactly on EOF.
 */
typedef struct xx_ea {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ea;

typedef xx_ea xx_ea_t;

XXFC_API void xx_ea_init(xx_ea *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ea *xx_ea_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ea_destroy(xx_ea *archive);
XXFC_API void xx_ea_free(xx_ea *archive);

XXFC_API bool xx_ea_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ea_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ea_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ea_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ea_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ea_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ea_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ea_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ea_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EA_H */
