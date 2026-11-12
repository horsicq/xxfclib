/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cork.h @brief CORK (Omega Logic installer member) reader. */

#ifndef XXFCLIB_FORMAT_CORK_H
#define XXFCLIB_FORMAT_CORK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CORK file: a 33 byte header ("CORK", version 1, the original 8.3
 * name, a CRC-16 of the plaintext, a DOS stamp and both sizes) followed by
 * one ARC method 8 (LZW plus RLE90) stream.  Exactly one member.
 */
typedef struct xx_cork {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cork;

typedef xx_cork xx_cork_t;

XXFC_API void xx_cork_init(xx_cork *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_cork *xx_cork_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cork_destroy(xx_cork *archive);
XXFC_API void xx_cork_free(xx_cork *archive);

XXFC_API bool xx_cork_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cork_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cork_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_cork_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cork_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cork_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cork_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cork_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cork_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cork_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cork_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cork_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cork_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cork_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CORK_H */
