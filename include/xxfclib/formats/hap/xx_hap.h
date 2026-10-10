/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_hap.h @brief HAP archive reader. */

#ifndef XXFCLIB_FORMAT_HAP_H
#define XXFCLIB_FORMAT_HAP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A HAP archive by Harri Hirvola: a 15-byte signature block followed by a flat run of 40-byte member headers, each carrying its own signature and immediately
 * followed by its payload, stored or packed with HAP's order-5 PPM coder.
 */
typedef struct xx_hap {
    Abstractformat format;
    uint64_t number_of_records;
} xx_hap;

typedef xx_hap xx_hap_t;

XXFC_API void xx_hap_init(xx_hap *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_hap *xx_hap_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hap_destroy(xx_hap *archive);
XXFC_API void xx_hap_free(xx_hap *archive);

XXFC_API bool xx_hap_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hap_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hap_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_hap_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hap_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hap_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hap_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hap_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hap_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_hap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_hap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_HAP_H */
