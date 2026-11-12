/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DWC_H
#define XXFCLIB_FORMAT_DWC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded DWC STORE and bounded MSB9-15 LZW (data codes bounded by16MiB member limit), packed CRC-16/ARC.
 * Periodic000, standalone resets or bounded raw blocks; resets require a fresh literal. No optional paths/comments or installer actions. */
typedef struct xx_dwc {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_dwc;
typedef struct xx_dwc xx_dwc_t;
XXFC_API void xx_dwc_init(xx_dwc *, xx_io_device *, int64_t);
XXFC_API xx_dwc *xx_dwc_create(xx_io_device *, int64_t);
XXFC_API void xx_dwc_destroy(xx_dwc *);
XXFC_API void xx_dwc_free(xx_dwc *);
XXFC_API bool xx_dwc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dwc_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dwc_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_dwc_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_dwc_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_dwc_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dwc_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_dwc_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_dwc_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dwc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dwc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dwc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dwc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dwc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
