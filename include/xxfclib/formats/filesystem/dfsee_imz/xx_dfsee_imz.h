/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DFSEE_IMZ_H
#define XXFCLIB_FORMAT_DFSEE_IMZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A single DFSee image span, with Tpack# LZW12/RLE units and original CRC32s.
 * SMART omitted areas are represented as zeros, not recovered original bytes.
 * disk.img contains the captured sectors, without padding to the enclosing
 * object's size or prefixing the source-sector offset. */
typedef struct xx_dfsee_imz {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint64_t image_size;
    uint64_t source_offset;
    uint32_t unit_capacity;
} xx_dfsee_imz;
typedef struct xx_dfsee_imz xx_dfsee_imz_t;
XXFC_API void xx_dfsee_imz_init(xx_dfsee_imz *, xx_io_device *, int64_t);
XXFC_API xx_dfsee_imz *xx_dfsee_imz_create(xx_io_device *, int64_t);
XXFC_API void xx_dfsee_imz_destroy(xx_dfsee_imz *);
XXFC_API void xx_dfsee_imz_free(xx_dfsee_imz *);
XXFC_API bool xx_dfsee_imz_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dfsee_imz_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dfsee_imz_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_dfsee_imz_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_dfsee_imz_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_dfsee_imz_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dfsee_imz_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_dfsee_imz_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_dfsee_imz_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dfsee_imz_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_dfsee_imz_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_dfsee_imz_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_dfsee_imz_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dfsee_imz_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
