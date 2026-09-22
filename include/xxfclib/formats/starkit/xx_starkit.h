/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_starkit.h @brief A Tcl Starkit: a Metakit v4 column-store database carrying the mk4vfs schema dirs[name,parent,files[name,size,date,contents]]. */

#ifndef XXFCLIB_FORMAT_STARKIT_H
#define XXFCLIB_FORMAT_STARKIT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Tcl Starkit: a Metakit v4 column-store database carrying the mk4vfs schema dirs[name,parent,files[name,size,date,contents]].
 */
typedef struct xx_starkit {
    Abstractformat format;
    uint64_t number_of_records;
} xx_starkit;

typedef xx_starkit xx_starkit_t;

XXFC_API void xx_starkit_init(xx_starkit *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_starkit *xx_starkit_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_starkit_destroy(xx_starkit *archive);
XXFC_API void xx_starkit_free(xx_starkit *archive);

XXFC_API bool xx_starkit_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_starkit_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_starkit_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_starkit_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_starkit_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_starkit_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_starkit_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_starkit_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_starkit_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STARKIT_H */
