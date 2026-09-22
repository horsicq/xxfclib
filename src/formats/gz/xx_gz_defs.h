/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_GZ_DEFS_H
#define XXFCLIB_FORMAT_GZ_DEFS_H

#define XX_GZ_ID1 0x1FU
#define XX_GZ_ID2 0x8BU
#define XX_GZ_CM_DEFLATE 8U

#define XX_GZ_FLAG_FTEXT    0x01U
#define XX_GZ_FLAG_FHCRC    0x02U
#define XX_GZ_FLAG_FEXTRA   0x04U
#define XX_GZ_FLAG_FNAME    0x08U
#define XX_GZ_FLAG_FCOMMENT 0x10U
#define XX_GZ_FLAG_RESERVED 0xE0U

#define XX_GZ_HEADER_SIZE 10U
#define XX_GZ_TRAILER_SIZE 8U
#define XX_GZ_MAX_OPTIONAL_STRING (1024U * 1024U)

#pragma pack(push, 1)
typedef struct xx_gz_header_s {
    uint8_t id1;
    uint8_t id2;
    uint8_t compression_method;
    uint8_t flags;
    uint32_t modification_time;
    uint8_t extra_flags;
    uint8_t operating_system;
} xx_gz_header;

typedef struct xx_gz_trailer_s {
    uint32_t crc32;
    uint32_t input_size;
} xx_gz_trailer;
#pragma pack(pop)

#endif /* XXFCLIB_FORMAT_GZ_DEFS_H */
