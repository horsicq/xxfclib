/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wim.h @brief Microsoft Windows Imaging (WIM) container. */

#ifndef XXFCLIB_FORMAT_WIM_H
#define XXFCLIB_FORMAT_WIM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A WIM: the header's offset table lists every stream the image holds.
 * Streams are content addressed by SHA-1 and may be LZX or XPRESS coded. */
typedef struct xx_wim {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_wim;

typedef struct xx_wim xx_wim_t;

XXFC_API void xx_wim_init(xx_wim *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_wim *xx_wim_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_wim_destroy(xx_wim *archive);
XXFC_API void xx_wim_free(xx_wim *archive);
XXFC_API bool xx_wim_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wim_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wim_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_wim_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_wim_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wim_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wim_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wim_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wim_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_wim_to_format(xx_wim *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WIM_H */
