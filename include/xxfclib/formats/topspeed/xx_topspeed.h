/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_topspeed.h @brief TopSpeed (Clarion/JPI) installer member reader. */

#ifndef XXFCLIB_FORMAT_TOPSPEED_H
#define XXFCLIB_FORMAT_TOPSPEED_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration pending.
 *
 * xxfc_defs.h carries no XX_FILE_TYPE_TOPSPEED enumerator yet, and that
 * header is shared, so it is not edited from here. The provisional number
 * below keeps this reader self-contained and compilable; the integrator
 * adding the real enumerator should redefine this to it. The value is NOT
 * part of the on-disk format. */
#ifndef XX_TOPSPEED_FILE_TYPE_ID
#define XX_TOPSPEED_FILE_TYPE_ID XX_FILE_TYPE_TOPSPEED
#endif

/**
 * @brief A TopSpeed installer member: one implicit record covering the whole
 *        file, which is a chain of six-byte-headed, individually checksummed
 *        blocks. There is no signature and no directory.
 */
typedef struct xx_topspeed {
    Abstractformat format;
    uint64_t number_of_records;
    /** Plaintext length the block chain adds up to; 0 until parsed. */
    uint64_t uncompressed_size;
} xx_topspeed;

typedef xx_topspeed xx_topspeed_t;

XXFC_API void xx_topspeed_init(xx_topspeed *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_topspeed *xx_topspeed_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_topspeed_destroy(xx_topspeed *archive);
XXFC_API void xx_topspeed_free(xx_topspeed *archive);

XXFC_API bool xx_topspeed_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_topspeed_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_topspeed_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_topspeed_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_topspeed_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_topspeed_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_topspeed_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_topspeed_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_topspeed_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TOPSPEED_H */
