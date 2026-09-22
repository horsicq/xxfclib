/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_XZ_DEFS_H
#define XXFCLIB_FORMAT_XZ_DEFS_H

#define XX_XZ_STREAM_HEADER_SIZE 12
#define XX_XZ_STREAM_FOOTER_SIZE 12
#define XX_XZ_MAGIC_SIZE 6
#define XX_XZ_MAX_BLOCKS 1000000U
#define XX_XZ_MAX_STREAMS 100000U
#define XX_XZ_MAX_INDEX_SIZE (64U * 1024U * 1024U)
#define XX_XZ_INDEX_VALIDATION_WORK_FACTOR 4U
#define XX_XZ_FILTER_LZMA2 0x21U
#define XX_XZ_FILTER_DELTA 0x03U
#define XX_XZ_CHECK_NONE 0U
#define XX_XZ_CHECK_CRC32 1U
#define XX_XZ_CHECK_CRC64 4U
#define XX_XZ_CHECK_SHA256 10U
#define XX_XZ_MAX_FILTERS 4U
#define XX_XZ_MAX_FILTER_PROPERTIES 1024U
#define XX_XZ_PAYLOAD_NAME "payload"
#define XX_XZ_COMPRESSION_METHOD 0x21U

static const unsigned char XX_XZ_MAGIC[XX_XZ_MAGIC_SIZE] = {
    0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00
};

#pragma pack(push, 1)
typedef struct xx_xz_stream_header_s {
    unsigned char magic[6];
    unsigned char flags[2];
    unsigned char crc32[4];
} xx_xz_stream_header;

typedef struct xx_xz_stream_footer_s {
    unsigned char crc32[4];
    unsigned char backward_size[4];
    unsigned char flags[2];
    unsigned char magic[2];
} xx_xz_stream_footer;
#pragma pack(pop)

#endif /* XXFCLIB_FORMAT_XZ_DEFS_H */
