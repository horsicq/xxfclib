/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_ELM_H
#define XXFCLIB_FORMAT_ELM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stored Microsoft theme ELM resource bundle. */
typedef struct xx_elm {
    Abstractformat format;
    uint64_t number_of_records;
} xx_elm;

XXFC_API void xx_elm_init(xx_elm *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_elm *xx_elm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_elm_destroy(xx_elm *archive);
XXFC_API void xx_elm_free(xx_elm *archive);
XXFC_API bool xx_elm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
/** True for a valid ELM bundle with unlisted CSS after the declared chain.
 * Used to distinguish it from an exact-ended FrontPage theme at detection. */
XXFC_API bool xx_elm_is_css_trailer_variant(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_elm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_elm_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_elm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_elm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_elm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_elm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_elm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_elm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_elm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_elm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_elm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
