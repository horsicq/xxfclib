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
 * Two signatures are recognised, both decoded:
 *
 *   "BCM!"  BCM v1.10 beta and later (1.30 is the common release).  After
 *           the magic the whole file is one binary arithmetic-coded
 *           bitstream carrying
 *             repeat { u32 block_length (0 ends the list)
 *                      u32 bwt_primary_index
 *                      block_length bytes of the BWT output, each coded
 *                      by an order-0/order-1/order-2 counter mix + SSE }
 *             u32 CRC-32 of the original data
 *           with the u32 fields coded as 32 flat (p = 1/2) bits.
 *
 *   "BCM1"  BCM v1.00 .. v1.04.  The same layout and model with a different
 *           counter mix and SSE start value; the u32 fields are coded as four
 *           model bytes, most significant first, and there is no CRC-32.
 *
 *   There is no member name and no stored total size.  This reader presents
 *   a single member, "payload".  The declared block length only bounds
 *   memory: the buffers grow as symbols are actually decoded, up to a
 *   256 MiB block ceiling.  "BCM2".."BCM9" were never written by a 1.xx
 *   release and are refused.
 */
typedef struct xx_bcm {
    Abstractformat format;
    uint8_t version; /**< 1 for "BCM1"; 0 for "BCM!". */
    uint8_t signature; /**< The fourth magic byte ('!' or '1'). */
    uint64_t number_of_records; /**< 1 once the stream is recognised. */
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

/** @brief 1 for a "BCM1" stream, or 0 ("BCM!" or header not read). */
XXFC_API uint8_t xx_bcm_get_version(const xx_bcm *archive);

/**
 * @brief Decode the "BCM!" or "BCM1" stream at the reader's base address
 * into @p destination (NULL: decode and verify only).
 *
 * Fails on corrupt input (including a stream that runs out before its end
 * marker), on a block above the 256 MiB ceiling or larger than the first
 * block, and when a "BCM!" stream's stored CRC-32 does not match.  @p out_size
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

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bcm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bcm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bcm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_BCM_H */
