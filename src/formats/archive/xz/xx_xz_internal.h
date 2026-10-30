/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_XZ_INTERNAL_H
#define XXFCLIB_XZ_INTERNAL_H

/* Reader state and helpers shared by xx_xz.c and xx_xz_datastructs.c. */

#include "xxfclib/data/xx_pd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct xx_xz_block_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t padding_offset;
    int64_t padding_size;
    int64_t check_offset;
    int64_t check_size;
    uint64_t uncompressed_size;
    uint64_t unpadded_size;
    uint8_t flags;
    uint8_t filter_count;
    uint8_t lzma2_property;
    uint16_t delta_distance;
    uint64_t bcj_method;
    uint8_t bcj_properties[4];
    uint8_t bcj_properties_size;
    bool delta_after_bcj;
    /* Reversible filters before LZMA2 in a multi-filter Block. */
    uint8_t chain_kind[3]; /* 1 = BCJ, 2 = Delta */
    uint16_t chain_distance[3];
    uint64_t chain_method[3];
    uint8_t chain_properties[3][4];
    uint8_t chain_property_size[3];
    bool extractable;
} xx_xz_block;

typedef struct xx_xz_stream_s {
    int64_t header_offset;
    int64_t index_offset;
    int64_t index_size;
    int64_t footer_offset;
    int64_t stream_end;
    int64_t padding_offset;
    int64_t padding_size;
    size_t first_block;
    size_t block_count;
    uint8_t check_type;
} xx_xz_stream;

typedef struct xx_xz_private_s {
    xx_xz_stream *streams;
    size_t stream_count;
    xx_xz_block *blocks;
    size_t block_count;
    int64_t format_end;
    uint64_t uncompressed_size;
    uint64_t compressed_data_size;
    bool can_extract;
} xx_xz_private;

bool xx_xz_vli(const uint8_t *data, size_t limit, size_t *cursor, uint64_t *value);
bool xx_xz_crc32_cancellable(const uint8_t *data, size_t size, xx_pd_struct *pd, uint32_t *result);

#endif /* XXFCLIB_XZ_INTERNAL_H */
