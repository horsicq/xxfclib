/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lofi.h @brief Solaris lofi image reader. */

#ifndef XXFCLIB_FORMAT_LOFI_H
#define XXFCLIB_FORMAT_LOFI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Solaris compressed lofi disk image written by `lofiadm -C lzma`: a big-endian header naming the algorithm and the segment geometry, an index of segment offsets, and one independent LZMA stream per segment forming a single logical image.
 */
typedef struct xx_lofi {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lofi;

typedef xx_lofi xx_lofi_t;

XXFC_API void xx_lofi_init(xx_lofi *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lofi *xx_lofi_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lofi_destroy(xx_lofi *archive);
XXFC_API void xx_lofi_free(xx_lofi *archive);

XXFC_API bool xx_lofi_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lofi_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lofi_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lofi_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lofi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lofi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lofi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lofi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lofi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LOFI_H */
