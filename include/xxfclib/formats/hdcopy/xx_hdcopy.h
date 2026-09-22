/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_hdcopy.h @brief HD-COPY floppy disk image. */

#ifndef XXFCLIB_FORMAT_HDCOPY_H
#define XXFCLIB_FORMAT_HDCOPY_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An HD-COPY image: a track usage map and one RLE block per used track,
 * presented as the single flat sector image they rebuild. */
typedef struct xx_hdcopy {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_hdcopy;

typedef struct xx_hdcopy xx_hdcopy_t;

XXFC_API void xx_hdcopy_init(xx_hdcopy *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_hdcopy *xx_hdcopy_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hdcopy_destroy(xx_hdcopy *archive);
XXFC_API void xx_hdcopy_free(xx_hdcopy *archive);
XXFC_API bool xx_hdcopy_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hdcopy_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hdcopy_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_hdcopy_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_hdcopy_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hdcopy_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hdcopy_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hdcopy_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hdcopy_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_hdcopy_to_format(xx_hdcopy *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HDCOPY_H */
