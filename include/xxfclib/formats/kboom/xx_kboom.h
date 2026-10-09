/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_kboom.h @brief K-BOOM archive reader. */

#ifndef XXFCLIB_FORMAT_KBOOM_H
#define XXFCLIB_FORMAT_KBOOM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A K-BOOM archive: a 12-byte header carrying a 32-bit magic, the plaintext length and a DOS timestamp, followed by a single K-BOOM LZW stream running to end-of-file.
 */
typedef struct xx_kboom {
    Abstractformat format;
    uint64_t number_of_records;
} xx_kboom;

typedef xx_kboom xx_kboom_t;

XXFC_API void xx_kboom_init(xx_kboom *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_kboom *xx_kboom_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_kboom_destroy(xx_kboom *archive);
XXFC_API void xx_kboom_free(xx_kboom *archive);

XXFC_API bool xx_kboom_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_kboom_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_kboom_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_kboom_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_kboom_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kboom_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kboom_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kboom_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kboom_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_kboom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_kboom_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_kboom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_kboom_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_kboom_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_KBOOM_H */
