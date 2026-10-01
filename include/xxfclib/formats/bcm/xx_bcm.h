/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bcm.h @brief BCM (BWT + context mixing) compressed stream reader. */

#ifndef XXFCLIB_FORMAT_BCM_H
#define XXFCLIB_FORMAT_BCM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BCM stream (Ilya Muravyov's BWT-based compressor).
 *
 * Two signatures are recognised:
 *
 *   "BCM!"  BCM 1.xx (1.30 is the current release).  After the magic the
 *           whole file is one binary arithmetic-coded bitstream carrying
 *             repeat { u32 block_length (0 ends the list)
 *                      u32 bwt_primary_index
 *                      block_length bytes of the BWT output, each coded
 *                      by an order-0/order-1/order-2 counter mix + SSE }
 *             u32 CRC-32 of the original data
 *           There is no member name and no stored total size.  This reader
 *           decodes it and presents a single member, "payload".  The
 *           declared block length only bounds memory: the buffers grow as
 *           symbols are actually decoded, up to a 256 MiB block ceiling.
 *
 *   "BCM1".."BCM9"  earlier 0.xx releases.  Their bitstream is not decoded:
 *           these are identified and sized only and report zero records.
 */
typedef struct xx_bcm {
    Abstractformat format;
    uint8_t version; /**< The digit of an old "BCM<n>" magic; 0 for "BCM!". */
    uint8_t signature; /**< The fourth magic byte ('!' or '1'..'9'). */
    uint64_t number_of_records; /**< 1 for "BCM!", 0 for the old streams. */
} xx_bcm;

typedef xx_bcm xx_bcm_t;
typedef xx_bcm XBcm;

XXFC_API void xx_bcm_init(xx_bcm *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_bcm *xx_bcm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bcm_destroy(xx_bcm *archive);
XXFC_API void xx_bcm_free(xx_bcm *archive);

XXFC_API bool xx_bcm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bcm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bcm_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bcm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bcm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bcm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bcm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bcm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bcm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief The old-format version digit, or 0 ("BCM!" or header not read). */
XXFC_API uint8_t xx_bcm_get_version(const xx_bcm *archive);

/**
 * @brief Decode the "BCM!" stream at the reader's base address into
 * @p destination (NULL: decode and verify only).
 *
 * Fails on an old "BCM<n>" stream, on corrupt input, on a block above the
 * 256 MiB ceiling, and when the stored CRC-32 does not match.  @p out_size
 * and @p consumed (both optional) receive the decoded length and the number
 * of input bytes the stream occupies, magic included.
 */
XXFC_API bool xx_bcm_unpack_to_device(xx_bcm *archive,
                                      xx_io_device *destination,
                                      uint64_t *out_size, int64_t *consumed,
                                      xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BCM_H */
