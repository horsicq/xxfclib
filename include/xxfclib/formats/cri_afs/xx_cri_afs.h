/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cri_afs.h @brief CRI AFS archive reader. */

#ifndef XXFCLIB_FORMAT_CRI_AFS_H
#define XXFCLIB_FORMAT_CRI_AFS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CRI AFS container. */
typedef struct xx_cri_afs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
} xx_cri_afs;

typedef xx_cri_afs xx_cri_afs_t;

XXFC_API void xx_cri_afs_init(xx_cri_afs *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_cri_afs *xx_cri_afs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cri_afs_destroy(xx_cri_afs *archive);
XXFC_API void xx_cri_afs_free(xx_cri_afs *archive);

XXFC_API bool xx_cri_afs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cri_afs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cri_afs_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_cri_afs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cri_afs_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cri_afs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cri_afs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cri_afs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cri_afs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cri_afs_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cri_afs_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cri_afs_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cri_afs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cri_afs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CRI_AFS_H */
