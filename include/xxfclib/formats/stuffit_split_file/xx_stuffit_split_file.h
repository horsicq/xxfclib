/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_STUFFIT_SPLIT_FILE_H
#define XXFCLIB_FORMAT_STUFFIT_SPLIT_FILE_H

#include "xxfclib/formats/xx_format.h"

/* StuffIt split file (StuffIt "Segment" output, "name.1", "name.2", ...):
 * every segment starts with a 100-byte header naming the original Mac file;
 * the payloads of all segments, joined in part order, are the file's
 * resource fork followed by its data fork. */
typedef struct xx_stuffit_split_file {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t part_number; /* 1-based segment number from the header */
    uint32_t rsrc_length; /* whole resource fork, over all segments */
    uint32_t data_length; /* whole data fork, over all segments */
    bool is_complete;     /* this device holds both forks entirely */
} xx_stuffit_split_file;

XXFC_API void xx_stuffit_split_file_init(xx_stuffit_split_file *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_stuffit_split_file *xx_stuffit_split_file_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stuffit_split_file_destroy(xx_stuffit_split_file *archive);
XXFC_API void xx_stuffit_split_file_free(xx_stuffit_split_file *archive);
XXFC_API bool xx_stuffit_split_file_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_stuffit_split_file_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_stuffit_split_file_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_stuffit_split_file_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_stuffit_split_file_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stuffit_split_file_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stuffit_split_file_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stuffit_split_file_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stuffit_split_file_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_stuffit_split_file_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_stuffit_split_file_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_stuffit_split_file_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_stuffit_split_file_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_stuffit_split_file_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
