/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cvrb.h @brief CVRB (Caere installer member) reader. */

#ifndef XXFCLIB_FORMAT_CVRB_H
#define XXFCLIB_FORMAT_CVRB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CVRB file: a 19 byte header ("CVRB", DOS stamp, unpacked size,
 * packed size, CRC-16/KERMIT of the plaintext, method 0) followed by one
 * LSB-first LZW stream of 9 to 12 bit codes.  Exactly one member.
 */
typedef struct xx_cvrb {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cvrb;

typedef xx_cvrb xx_cvrb_t;

XXFC_API void xx_cvrb_init(xx_cvrb *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_cvrb *xx_cvrb_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cvrb_destroy(xx_cvrb *archive);
XXFC_API void xx_cvrb_free(xx_cvrb *archive);

XXFC_API bool xx_cvrb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cvrb_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cvrb_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_cvrb_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cvrb_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cvrb_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cvrb_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cvrb_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cvrb_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cvrb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cvrb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cvrb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cvrb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cvrb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CVRB_H */
