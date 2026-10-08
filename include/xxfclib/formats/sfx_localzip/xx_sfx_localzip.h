/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sfx_localzip.h @brief Validated embedded SFX archive records. */

#ifndef XXFCLIB_FORMAT_SFX_LOCALZIP_H
#define XXFCLIB_FORMAT_SFX_LOCALZIP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An executable containing validated embedded archive records.
 */
typedef struct xx_sfx_localzip {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sfx_localzip;

typedef xx_sfx_localzip xx_sfx_localzip_t;

XXFC_API void xx_sfx_localzip_init(xx_sfx_localzip *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sfx_localzip *xx_sfx_localzip_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sfx_localzip_destroy(xx_sfx_localzip *archive);
XXFC_API void xx_sfx_localzip_free(xx_sfx_localzip *archive);

XXFC_API bool xx_sfx_localzip_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfx_localzip_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_localzip_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_localzip_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sfx_localzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfx_localzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_localzip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_localzip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_localzip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API bool xx_sfx_localzip_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_localzip_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_localzip_get_abstract_extractor(void);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_LOCALZIP_H */
