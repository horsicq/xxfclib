/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cpz.h @brief Central Point CPZ archive reader. */

#ifndef XXFCLIB_FORMAT_CPZ_H
#define XXFCLIB_FORMAT_CPZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Central Point CPZ archive: a member count and a CRC-32 of the
 * directory, 32-byte directory records (packed CRC-32, 8.3 name, attributes,
 * method, both sizes, DOS time and date) and the payloads back to back,
 * stored or PKWARE DCL imploded.
 */
typedef struct xx_cpz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cpz;

typedef xx_cpz xx_cpz_t;

XXFC_API void xx_cpz_init(xx_cpz *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_cpz *xx_cpz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cpz_destroy(xx_cpz *archive);
XXFC_API void xx_cpz_free(xx_cpz *archive);

XXFC_API bool xx_cpz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cpz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cpz_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_cpz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cpz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cpz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cpz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cpz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cpz_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cpz_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cpz_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cpz_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cpz_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CPZ_H */
