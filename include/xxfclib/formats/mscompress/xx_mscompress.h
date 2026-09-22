/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mscompress.h @brief Microsoft SZDD and single-volume SZ reader. */

#ifndef XXFCLIB_FORMAT_MSCOMPRESS_H
#define XXFCLIB_FORMAT_MSCOMPRESS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_mscompress_variant_e {
    XX_MSCOMPRESS_VARIANT_UNKNOWN = 0,
    XX_MSCOMPRESS_VARIANT_SZDD = 1,
    XX_MSCOMPRESS_VARIANT_SZ = 2
} xx_mscompress_variant_t;

typedef struct xx_mscompress {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    xx_mscompress_variant_t variant;
    uint8_t missing_filename_char;
} xx_mscompress;

typedef xx_mscompress xx_mscompress_t;
typedef xx_mscompress XMsCompress;

XXFC_API void xx_mscompress_init(xx_mscompress *archive,
                                 xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_mscompress *xx_mscompress_create(xx_io_device *device,
                                              int64_t base_address);
XXFC_API void xx_mscompress_destroy(xx_mscompress *archive);
XXFC_API void xx_mscompress_free(xx_mscompress *archive);

XXFC_API bool xx_mscompress_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_mscompress_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_mscompress_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_mscompress_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the complete SZDD or one-volume SZ member to a device. */
XXFC_API bool xx_mscompress_unpack_to_device(xx_mscompress *archive,
                                              xx_io_device *destination,
                                              xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mscompress_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mscompress_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mscompress_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mscompress_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mscompress_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_mscompress_get_uncompressed_size(
    const xx_mscompress *archive);
XXFC_API int64_t xx_mscompress_get_stream_end(const xx_mscompress *archive);
XXFC_API xx_mscompress_variant_t xx_mscompress_get_variant(
    const xx_mscompress *archive);
XXFC_API uint8_t xx_mscompress_get_missing_filename_char(
    const xx_mscompress *archive);

static inline Abstractformat *xx_mscompress_to_format(xx_mscompress *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MSCOMPRESS_H */
