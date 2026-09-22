/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_XX_BZ2_DEFS_H
#define XXFCLIB_XX_BZ2_DEFS_H

#include <stdint.h>

#define XX_BZ2_HEADER_SIZE 4
#define XX_BZ2_MIN_STREAM_SIZE 14
#define XX_BZ2_PAYLOAD_NAME "payload"
#define XX_BZ2_COMPRESSION_METHOD 12U

typedef struct xx_bz2_stream_header_s {
    uint8_t magic[3];
    uint8_t block_size;
} xx_bz2_stream_header;

#endif /* XXFCLIB_XX_BZ2_DEFS_H */
