/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_quake_pak.h @brief Quake PAK archive reader. */

#ifndef XXFCLIB_FORMAT_QUAKE_PAK_H
#define XXFCLIB_FORMAT_QUAKE_PAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Quake PAK container. */
typedef struct xx_quake_pak {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_quake_pak;

typedef xx_quake_pak xx_quake_pak_t;

XXFC_API void xx_quake_pak_init(xx_quake_pak *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_quake_pak *xx_quake_pak_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_quake_pak_destroy(xx_quake_pak *archive);
XXFC_API void xx_quake_pak_free(xx_quake_pak *archive);

XXFC_API bool xx_quake_pak_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_quake_pak_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_quake_pak_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_quake_pak_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_quake_pak_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_quake_pak_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_quake_pak_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_quake_pak_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_quake_pak_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_quake_pak_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_quake_pak_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_quake_pak_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_quake_pak_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_quake_pak_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_QUAKE_PAK_H */
