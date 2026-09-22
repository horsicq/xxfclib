/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ealib.h @brief EALIB archive reader. */

#ifndef XXFCLIB_FORMAT_EALIB_H
#define XXFCLIB_FORMAT_EALIB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An EALIB archive: a seven-byte header, a directory of fixed-width entries with one extra sentinel entry that supplies the end offset of the last member, and a payload area the entries must tile exactly from the end of the directory to EOF.
 */
typedef struct xx_ealib {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ealib;

typedef xx_ealib xx_ealib_t;

XXFC_API void xx_ealib_init(xx_ealib *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ealib *xx_ealib_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ealib_destroy(xx_ealib *archive);
XXFC_API void xx_ealib_free(xx_ealib *archive);

XXFC_API bool xx_ealib_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ealib_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ealib_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ealib_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ealib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ealib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ealib_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ealib_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ealib_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EALIB_H */
