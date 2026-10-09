/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mlb_ft.h @brief A stored MLB_FT archive with a 21-byte record directory. */

#ifndef XXFCLIB_FORMAT_MLB_FT_H
#define XXFCLIB_FORMAT_MLB_FT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A stored MLB_FT archive with a 21-byte record directory.
 */
typedef struct xx_mlb_ft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mlb_ft;

typedef xx_mlb_ft xx_mlb_ft_t;

XXFC_API void xx_mlb_ft_init(xx_mlb_ft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_mlb_ft *xx_mlb_ft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_mlb_ft_destroy(xx_mlb_ft *archive);
XXFC_API void xx_mlb_ft_free(xx_mlb_ft *archive);

XXFC_API bool xx_mlb_ft_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mlb_ft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_mlb_ft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_mlb_ft_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mlb_ft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mlb_ft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mlb_ft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mlb_ft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mlb_ft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API bool xx_mlb_ft_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mlb_ft_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mlb_ft_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mlb_ft_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mlb_ft_get_abstract_detector(void);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MLB_FT_H */
