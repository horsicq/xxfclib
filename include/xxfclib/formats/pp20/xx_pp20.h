/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pp20.h @brief PowerPacker (PP20 / PP11 / PPLS) single-stream reader. */

#ifndef XXFCLIB_FORMAT_PP20_H
#define XXFCLIB_FORMAT_PP20_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PowerPacker crunched data file.  One stream, no member names. */
typedef struct xx_pp20 {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t variant;       /**< 0 = PP20, 1 = PP11, 2 = PPLS, 3 = encrypted */
    uint8_t offset_widths[4];
    uint8_t skip_bits;
} xx_pp20;

typedef xx_pp20 xx_pp20_t;
typedef xx_pp20 XPP20;

XXFC_API void xx_pp20_init(xx_pp20 *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_pp20 *xx_pp20_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pp20_destroy(xx_pp20 *archive);
XXFC_API void xx_pp20_free(xx_pp20 *archive);

XXFC_API bool xx_pp20_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pp20_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pp20_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_pp20_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_pp20_unpack_to_device(xx_pp20 *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pp20_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pp20_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pp20_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pp20_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pp20_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_pp20_get_uncompressed_size(const xx_pp20 *archive);
XXFC_API int64_t xx_pp20_get_stream_end(const xx_pp20 *archive);

static inline Abstractformat *xx_pp20_to_format(xx_pp20 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PP20_H */
