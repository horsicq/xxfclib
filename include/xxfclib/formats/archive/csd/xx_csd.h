/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_csd.h @brief IBM OS/2 service-pack (CSD/FixPak) compressed member reader. */

#ifndef XXFCLIB_FORMAT_CSD_H
#define XXFCLIB_FORMAT_CSD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM OS/2 service-pack (CSD/FixPak) compressed file: a six byte
 * magic followed by one scrambled adaptive-Huffman LZ/phrase stream with an
 * end code.  Exactly one member, the plaintext, named after the container.
 */
typedef struct xx_csd {
    Abstractformat format;
    uint64_t number_of_records;
} xx_csd;

typedef xx_csd xx_csd_t;

XXFC_API void xx_csd_init(xx_csd *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_csd *xx_csd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_csd_destroy(xx_csd *archive);
XXFC_API void xx_csd_free(xx_csd *archive);

XXFC_API bool xx_csd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_csd_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_csd_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_csd_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_csd_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_csd_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_csd_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_csd_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_csd_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_csd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_csd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_csd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_csd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_csd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CSD_H */
