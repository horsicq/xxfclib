/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rtpatch.h @brief Pocket Soft RTPatch package reader. */

#ifndef XXFCLIB_FORMAT_RTPATCH_H
#define XXFCLIB_FORMAT_RTPATCH_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Pocket Soft RTPatch (.rtp) package: a 0x1a-byte header naming the builder generation, an optional counted-string directory table and banner, and then a run of records whose whole-file members are RTPatch-compressed streams preceded by a 34-byte 8.3 descriptor.
 */
typedef struct xx_rtpatch {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rtpatch;

typedef xx_rtpatch xx_rtpatch_t;

XXFC_API void xx_rtpatch_init(xx_rtpatch *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rtpatch *xx_rtpatch_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rtpatch_destroy(xx_rtpatch *archive);
XXFC_API void xx_rtpatch_free(xx_rtpatch *archive);

XXFC_API bool xx_rtpatch_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_rtpatch_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rtpatch_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rtpatch_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rtpatch_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rtpatch_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rtpatch_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rtpatch_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rtpatch_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RTPATCH_H */
