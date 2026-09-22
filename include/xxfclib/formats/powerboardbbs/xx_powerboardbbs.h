/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_powerboardbbs.h @brief Powerboard BBS library reader. */

#ifndef XXFCLIB_FORMAT_POWERBOARDBBS_H
#define XXFCLIB_FORMAT_POWERBOARDBBS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Powerboard BBS library: a headerless chain of stored records, each a lead byte carrying the name length and the width of the size field, a padded 8.3 name and the raw member data.
 */
typedef struct xx_powerboardbbs {
    Abstractformat format;
    uint64_t number_of_records;
} xx_powerboardbbs;

typedef xx_powerboardbbs xx_powerboardbbs_t;

XXFC_API void xx_powerboardbbs_init(xx_powerboardbbs *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_powerboardbbs *xx_powerboardbbs_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_powerboardbbs_destroy(xx_powerboardbbs *archive);
XXFC_API void xx_powerboardbbs_free(xx_powerboardbbs *archive);

XXFC_API bool xx_powerboardbbs_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_powerboardbbs_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_powerboardbbs_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_powerboardbbs_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_powerboardbbs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_powerboardbbs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_powerboardbbs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_powerboardbbs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_powerboardbbs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_POWERBOARDBBS_H */
