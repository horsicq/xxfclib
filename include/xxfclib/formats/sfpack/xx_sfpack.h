/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfpack.h @brief SFPack (.sfpack) SoundFont 2 container reader. */

#ifndef XXFCLIB_FORMAT_SFPACK_H
#define XXFCLIB_FORMAT_SFPACK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An SFPack file: the compressed pieces of ONE SoundFont 2.
 *
 * SFPACK IS NOT AN ARCHIVE.  It holds an INFO list, a pdta list and one
 * compressed stream per sample, and the codec rebuilds the single .sf2 those
 * pieces belong to - relaying out the sample data and rewriting every shdr
 * record to match.  So exactly one archive record is published, the whole file
 * is handed to the codec, and the container stores no member name.
 */
typedef struct xx_sfpack {
    Abstractformat format;      /**< Must stay first: the reader casts to it. */
    uint64_t number_of_records; /**< Always 1 for a valid container. */
    int64_t uncompressed_size;  /**< MEASURED .sf2 size, -1 when not measured. */
    int64_t declared_size;      /**< The unreliable i32 at +0x08. */
    uint16_t version;           /**< u16 at +0x04, always 0x0100. */
    uint16_t flags;             /**< u16 at +0x06. */
} xx_sfpack;

typedef xx_sfpack xx_sfpack_t;

XXFC_API void xx_sfpack_init(xx_sfpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sfpack *xx_sfpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sfpack_destroy(xx_sfpack *archive);
XXFC_API void xx_sfpack_free(xx_sfpack *archive);

XXFC_API bool xx_sfpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sfpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sfpack_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sfpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_sfpack_get_uncompressed_size(const xx_sfpack *archive);
XXFC_API int64_t xx_sfpack_get_declared_size(const xx_sfpack *archive);

static inline Abstractformat *xx_sfpack_to_format(xx_sfpack *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFPACK_H */
