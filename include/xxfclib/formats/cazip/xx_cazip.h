/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cazip.h @brief Computer Associates CAZIP / CAZIPXP reader. */

#ifndef XXFCLIB_FORMAT_CAZIP_H
#define XXFCLIB_FORMAT_CAZIP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which of the two CA containers this file is. */
typedef enum xx_cazip_variant_e {
    XX_CAZIP_VARIANT_NONE = 0,
    /** "\r\n\x1aCAZIPnn": one nameless PKWARE DCL stream behind a 20-byte
     *  binary head that carries the plaintext CRC-32. */
    XX_CAZIP_VARIANT_CLASSIC = 1,
    /** "CAZIP\x04": a multi-member archive of Unix-compress streams. */
    XX_CAZIP_VARIANT_XP = 2
} xx_cazip_variant;

/**
 * @brief A CAZIP or CAZIPXP container.
 *
 * The two are different containers that happen to share the vendor's name,
 * so one reader serves both and publishes which it found through @c variant.
 */
typedef struct xx_cazip {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t variant;
} xx_cazip;

typedef xx_cazip xx_cazip_t;

XXFC_API void xx_cazip_init(xx_cazip *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_cazip *xx_cazip_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_cazip_destroy(xx_cazip *archive);
XXFC_API void xx_cazip_free(xx_cazip *archive);

XXFC_API bool xx_cazip_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cazip_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_cazip_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_cazip_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cazip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cazip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cazip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cazip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cazip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CAZIP_H */
