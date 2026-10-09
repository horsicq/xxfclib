/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_T98_NEXT_NFD_H
#define XXFCLIB_FORMAT_T98_NEXT_NFD_H

#include "xxfclib/formats/xx_format.h"

/* T98-Next NFD floppy image (NEC PC-98), revisions r0 ("T98FDDIMAGE.R0")
 * and r1 ("T98FDDIMAGE.R1").  A header describes every sector (C/H/R/N and
 * controller status); the sector data follows the header, stored.  The
 * reader exposes one member, the plain sector dump ("image.img"). */
typedef struct xx_t98_next_nfd {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t revision;       /* 0 or 1 */
    uint32_t header_size;    /* dwHeadSize: offset of the first data byte */
    uint32_t heads;          /* byHead from the file header */
    uint32_t tracks;         /* tracks holding at least one sector */
    uint32_t sectors;        /* sectors in the dump */
    uint64_t image_size;     /* size of the dump */
} xx_t98_next_nfd;

XXFC_API void xx_t98_next_nfd_init(xx_t98_next_nfd *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_t98_next_nfd *xx_t98_next_nfd_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_t98_next_nfd_destroy(xx_t98_next_nfd *archive);
XXFC_API void xx_t98_next_nfd_free(xx_t98_next_nfd *archive);
XXFC_API bool xx_t98_next_nfd_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_t98_next_nfd_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_t98_next_nfd_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_t98_next_nfd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_t98_next_nfd_create_archive_records_reading(Abstractformat *self,
                                               const xx_list_s *options,
                                               xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_t98_next_nfd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_t98_next_nfd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_t98_next_nfd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_t98_next_nfd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_t98_next_nfd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_t98_next_nfd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_t98_next_nfd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_t98_next_nfd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_t98_next_nfd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
