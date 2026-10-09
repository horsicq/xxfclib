/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_thebat_msb.h @brief The Bat! MSB mailbox reader. */

#ifndef XXFCLIB_FORMAT_THEBAT_MSB_H
#define XXFCLIB_FORMAT_THEBAT_MSB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Bat! MSB mailbox reader.
 */
typedef struct xx_thebat_msb {
    Abstractformat format;
    uint64_t number_of_records;
} xx_thebat_msb;

typedef xx_thebat_msb xx_thebat_msb_t;

XXFC_API void xx_thebat_msb_init(xx_thebat_msb *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_thebat_msb *xx_thebat_msb_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_thebat_msb_destroy(xx_thebat_msb *archive);
XXFC_API void xx_thebat_msb_free(xx_thebat_msb *archive);

XXFC_API bool xx_thebat_msb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_thebat_msb_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_thebat_msb_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_thebat_msb_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_thebat_msb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_thebat_msb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_thebat_msb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_thebat_msb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_thebat_msb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API bool xx_thebat_msb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_thebat_msb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_thebat_msb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_thebat_msb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_thebat_msb_get_abstract_detector(void);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_THEBAT_MSB_H */
