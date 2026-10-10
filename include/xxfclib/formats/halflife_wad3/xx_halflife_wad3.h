/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_halflife_wad3.h @brief Half-Life WAD3 archive reader. */

#ifndef XXFCLIB_FORMAT_HALFLIFE_WAD3_H
#define XXFCLIB_FORMAT_HALFLIFE_WAD3_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Half-Life WAD3 container. */
typedef struct xx_halflife_wad3 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_halflife_wad3;

typedef xx_halflife_wad3 xx_halflife_wad3_t;

XXFC_API void xx_halflife_wad3_init(xx_halflife_wad3 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_halflife_wad3 *xx_halflife_wad3_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_halflife_wad3_destroy(xx_halflife_wad3 *archive);
XXFC_API void xx_halflife_wad3_free(xx_halflife_wad3 *archive);

XXFC_API bool xx_halflife_wad3_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_halflife_wad3_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_halflife_wad3_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_halflife_wad3_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_halflife_wad3_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_halflife_wad3_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_halflife_wad3_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_halflife_wad3_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_halflife_wad3_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_halflife_wad3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_halflife_wad3_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_halflife_wad3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_halflife_wad3_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_halflife_wad3_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_HALFLIFE_WAD3_H */
