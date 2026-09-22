/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ascend.h @brief Ascend compressed file reader. */

#ifndef XXFCLIB_FORMAT_ASCEND_H
#define XXFCLIB_FORMAT_ASCEND_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Ascend compressed file.
 *
 * Twelve bytes of date followed by a PKWARE DCL stream, and nothing else: no
 * magic, no name, no size, no checksum. Ascend's installers shipped these with
 * the last character of the extension replaced by an exclamation mark, so
 * INSTALL.INI became INSTALL.IN!.
 *
 * With no signature to match, identification rests on two things being true at
 * once: the twelve bytes must be a real calendar date and time, and the DCL
 * stream must decode cleanly and end exactly at the end of the file. Neither
 * alone would be enough -- a date is only bounded integers, and DCL has no
 * magic either -- but together they are specific enough that on the reference
 * corpus this matched all 32 members and nothing in a 4,573-file sweep.
 *
 * The member name cannot be recovered. It was the container's own file name
 * with the extension restored, and an xx_io_device has no name, so the single
 * member is called "ascend_data" -- the same fallback the reference uses when
 * it cannot see a file name either.
 */
typedef struct xx_ascend {
    Abstractformat format;
    uint64_t uncompressed_size;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t literal_mode;    /**< DCL literal coding mode, 0 or 1. */
    uint8_t dictionary_bits; /**< DCL window, 4 to 6. */
} xx_ascend;

typedef xx_ascend xx_ascend_t;
typedef xx_ascend XAscend;

XXFC_API void xx_ascend_init(xx_ascend *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ascend *xx_ascend_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ascend_destroy(xx_ascend *archive);
XXFC_API void xx_ascend_free(xx_ascend *archive);

XXFC_API bool xx_ascend_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ascend_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ascend_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ascend_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ascend_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ascend_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ascend_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ascend_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ascend_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief Decoded size of the single member. */
XXFC_API uint64_t xx_ascend_get_uncompressed_size(const xx_ascend *archive);
/** @brief The DCL window size in bits, 4 to 6. */
XXFC_API uint8_t xx_ascend_get_dictionary_bits(const xx_ascend *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ASCEND_H */
