/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_povlablzh.h @brief POVLAB LZH archive reader. */

#ifndef XXFCLIB_FORMAT_POVLABLZH_H
#define XXFCLIB_FORMAT_POVLABLZH_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A POVLAB LZH archive: a chain of LHA level-1 members whose five-byte method tag has been retagged "-ARS-" (stored) or "-ARA-" (-lh5-), terminated by a single zero byte.
 */
typedef struct xx_povlablzh {
    Abstractformat format;
    uint64_t number_of_records;
} xx_povlablzh;

typedef xx_povlablzh xx_povlablzh_t;

XXFC_API void xx_povlablzh_init(xx_povlablzh *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_povlablzh *xx_povlablzh_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_povlablzh_destroy(xx_povlablzh *archive);
XXFC_API void xx_povlablzh_free(xx_povlablzh *archive);

XXFC_API bool xx_povlablzh_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_povlablzh_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_povlablzh_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_povlablzh_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_povlablzh_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_povlablzh_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_povlablzh_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_povlablzh_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_povlablzh_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_povlablzh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_povlablzh_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_povlablzh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_povlablzh_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_povlablzh_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_POVLABLZH_H */
