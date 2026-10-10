/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_LZMA2_FILTERS_INTERNAL_H
#define XXFCLIB_LZMA2_FILTERS_INTERNAL_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/algo/hash/xx_hash.h"

/* Decode a dictionary property subject to XX_LZMA_MAX_DICT_SIZE. The output
 * pointer may be NULL when only support for the property needs checking. */
bool xx_lzma2_parse_property(uint8_t property, uint32_t *dictionary_size);

typedef struct xx_lzma2_decoded_info_s {
    uint64_t written;
    uint32_t crc32;
    uint64_t crc64;
    uint8_t sha256[XX_SHA256_DIGEST_SIZE];
} xx_lzma2_decoded_info;

enum {
    XX_LZMA2_CALC_CRC32 = 1U,
    XX_LZMA2_CALC_CRC64 = 2U,
    XX_LZMA2_CALC_SHA256 = 4U,
    XX_LZMA2_CALC_ALL = 7U
};

/* Compute only requested checks. Unrequested digest fields remain zero. */
bool xx_lzma2_unpack_filtered_checked_device(xx_io_device *source, int64_t offset, int64_t compressed_size, uint8_t property, uint16_t delta_distance,
                                             uint64_t expected_size, xx_io_device *destination, unsigned check_mask, xx_lzma2_decoded_info *info, xx_pd_struct *pd);

/* Decode a raw LZMA2 extent, optionally reversing a Delta filter (distance
 * 1..256; zero disables it). Enforce the expected decoded size and compute
 * checksums over the final output. A NULL destination validates only. Devices
 * are borrowed; a failed call may leave partial output in the destination.
 * Container framing and checksum comparison belong to the format reader. */
bool xx_lzma2_unpack_filtered_device(xx_io_device *source, int64_t offset, int64_t compressed_size, uint8_t property, uint16_t delta_distance, uint64_t expected_size,
                                     xx_io_device *destination, xx_lzma2_decoded_info *info, xx_pd_struct *pd);

#endif /* XXFCLIB_LZMA2_FILTERS_INTERNAL_H */
