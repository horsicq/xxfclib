/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vhddynamic.h @brief Microsoft VHD disk image (fixed, dynamic, differencing). */

#ifndef XXFCLIB_FORMAT_VHDDYNAMIC_H
#define XXFCLIB_FORMAT_VHDDYNAMIC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Microsoft VHD, presented as one flat disk stream ("disk.img"):
 *  - fixed: the raw disk followed by a "conectix" footer;
 *  - dynamic: footer copy, "cxsparse" dynamic-disk header, block
 *    allocation table and per-block sector bitmaps, then the footer;
 *  - differencing: laid out like dynamic; sectors the parent would supply
 *    read as zeros and the record comment names the parent.
 * The record's compression-method slot carries the disk type (2, 3, 4). */
typedef struct xx_vhddynamic {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vhddynamic;

typedef struct xx_vhddynamic xx_vhddynamic_t;

XXFC_API void xx_vhddynamic_init(xx_vhddynamic *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_vhddynamic *xx_vhddynamic_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vhddynamic_destroy(xx_vhddynamic *archive);
XXFC_API void xx_vhddynamic_free(xx_vhddynamic *archive);
XXFC_API bool xx_vhddynamic_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vhddynamic_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vhddynamic_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_vhddynamic_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vhddynamic_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vhddynamic_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vhddynamic_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vhddynamic_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vhddynamic_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vhddynamic_to_format(xx_vhddynamic *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VHDDYNAMIC_H */
