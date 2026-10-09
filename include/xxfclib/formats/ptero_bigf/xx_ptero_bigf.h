/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ptero_bigf.h @brief Ptero-Engine BIGF/ZBL (.cbf) reader. */

#ifndef XXFCLIB_FORMAT_PTERO_BIGF_H
#define XXFCLIB_FORMAT_PTERO_BIGF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Distinct from Electronic Arts BIGF/BIG4 archives. */
typedef struct xx_ptero_bigf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ptero_bigf;

XXFC_API void xx_ptero_bigf_init(xx_ptero_bigf *archive,
                                  xx_io_device *device, int64_t base_address);
XXFC_API xx_ptero_bigf *xx_ptero_bigf_create(xx_io_device *device,
                                              int64_t base_address);
XXFC_API void xx_ptero_bigf_destroy(xx_ptero_bigf *archive);
XXFC_API void xx_ptero_bigf_free(xx_ptero_bigf *archive);

XXFC_API bool xx_ptero_bigf_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_ptero_bigf_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_ptero_bigf_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_ptero_bigf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_ptero_bigf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ptero_bigf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ptero_bigf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ptero_bigf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ptero_bigf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ptero_bigf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ptero_bigf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ptero_bigf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ptero_bigf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ptero_bigf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_PTERO_BIGF_H */
