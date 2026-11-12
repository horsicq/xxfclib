/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cwc.h @brief CauseWay Compressor (CWC) data file reader. */

#ifndef XXFCLIB_FORMAT_CWC_H
#define XXFCLIB_FORMAT_CWC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CauseWay Compressor data file: a twelve byte header ("CWC", the
 * position width, the unpacked and packed sizes) followed by one LZ stream
 * with run and literal-string tokens.  Exactly one member, the plaintext.
 */
typedef struct xx_cwc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cwc;

typedef xx_cwc xx_cwc_t;

XXFC_API void xx_cwc_init(xx_cwc *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_cwc *xx_cwc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cwc_destroy(xx_cwc *archive);
XXFC_API void xx_cwc_free(xx_cwc *archive);

XXFC_API bool xx_cwc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cwc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cwc_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_cwc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cwc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cwc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cwc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cwc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cwc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cwc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cwc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cwc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cwc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cwc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CWC_H */
