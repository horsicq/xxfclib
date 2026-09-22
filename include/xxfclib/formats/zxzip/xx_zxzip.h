/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_zxzip.h @brief ZXZIP (ZX Spectrum "ZIP" archiver) member reader. */

#ifndef XXFCLIB_FORMAT_ZXZIP_H
#define XXFCLIB_FORMAT_ZXZIP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZXZIP archive: a 0x11-byte header carrying an eight-character
 * archive name, the literal "ZIP" and a Hobeta-style checksum, followed by a
 * chain of 0x16-byte directory entries each immediately followed by its own
 * packed bytes.
 *
 * A member is NOT emitted as its own bytes: every member, stored ones
 * included, is produced as a 17-byte Hobeta header plus the data plus zero
 * padding, so the published uncompressed size is that whole emitted file.
 */
typedef struct xx_zxzip {
    Abstractformat format;     /**< Must stay first: the reader casts to it. */
    uint64_t number_of_records; /**< Members found by the last parse. */
    int64_t archive_size;      /**< Bytes the entry/data chain occupies. */
} xx_zxzip;

typedef xx_zxzip xx_zxzip_t;

XXFC_API void xx_zxzip_init(xx_zxzip *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_zxzip *xx_zxzip_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_zxzip_destroy(xx_zxzip *archive);
XXFC_API void xx_zxzip_free(xx_zxzip *archive);

XXFC_API bool xx_zxzip_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zxzip_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_zxzip_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_zxzip_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zxzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zxzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zxzip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zxzip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zxzip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_zxzip_get_number_of_records(const xx_zxzip *archive);

static inline Abstractformat *xx_zxzip_to_format(xx_zxzip *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZXZIP_H */
