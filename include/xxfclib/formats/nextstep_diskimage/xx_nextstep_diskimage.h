/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_nextstep_diskimage.h @brief Read-only NeXTSTEP diskimage and
 * bounded legacy FFS traversal. The 46-byte geometry wrapper is validated;
 * the first valid 4.3BSD/NeXT UFS volume in the first MiB is exposed
 * through the Solaris UFS reader's explicitly enabled legacy profile.
 * Images without a supported volume retain one raw disk.img member.
 */
#ifndef XXFCLIB_FORMAT_NEXTSTEP_DISKIMAGE_H
#define XXFCLIB_FORMAT_NEXTSTEP_DISKIMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nextstep_diskimage {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t partition_offset;
    uint64_t disk_size;
} xx_nextstep_diskimage;
typedef xx_nextstep_diskimage xx_nextstep_diskimage_t;
XXFC_API void xx_nextstep_diskimage_init(xx_nextstep_diskimage *, xx_io_device *, int64_t);
XXFC_API xx_nextstep_diskimage *xx_nextstep_diskimage_create(xx_io_device *, int64_t);
XXFC_API void xx_nextstep_diskimage_destroy(xx_nextstep_diskimage *);
XXFC_API void xx_nextstep_diskimage_free(xx_nextstep_diskimage *);
XXFC_API bool xx_nextstep_diskimage_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nextstep_diskimage_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_nextstep_diskimage_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_nextstep_diskimage_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_nextstep_diskimage_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_nextstep_diskimage_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_nextstep_diskimage_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_nextstep_diskimage_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_nextstep_diskimage_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_nextstep_diskimage_to_format(xx_nextstep_diskimage *image)
{
    return image ? &image->format : NULL;
}
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nextstep_diskimage_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nextstep_diskimage_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nextstep_diskimage_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nextstep_diskimage_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nextstep_diskimage_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
