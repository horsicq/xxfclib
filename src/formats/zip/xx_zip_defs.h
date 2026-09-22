/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef XX_ZIP_DEFS_H
#define XX_ZIP_DEFS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard ZIP Record Signatures */
#define XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE          0x04034B50
#define XX_ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE   0x02014B50
#define XX_ZIP_EOCD_SIGNATURE                       0x06054B50
#define XX_ZIP_ZIP64_EOCD_SIGNATURE                 0x06064B50
#define XX_ZIP_ZIP64_EOCD_LOCATOR_SIGNATURE         0x07064B50
#define XX_ZIP_DATA_DESCRIPTOR_SIGNATURE            0x08074B50

#pragma pack(push, 1)

/**
 * @brief Local file header (30 bytes fixed)
 */
typedef struct {
    uint32_t signature;              /**< 0x04034B50 (PK\x03\x04) */
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
} xx_zip_local_header_t;

/**
 * @brief Central directory file header (46 bytes fixed)
 */
typedef struct {
    uint32_t signature;              /**< 0x02014B50 (PK\x01\x02) */
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t comment_length;
    uint16_t disk_number_start;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t relative_offset_local_header;
} xx_zip_central_directory_header_t;

/**
 * @brief End of Central Directory Record (EOCD, 22 bytes fixed)
 */
typedef struct {
    uint32_t signature;              /**< 0x06054B50 (PK\x05\x06) */
    uint16_t disk_number;
    uint16_t cd_start_disk;
    uint16_t records_on_disk;
    uint16_t total_records;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_length;
} xx_zip_eocd_t;

/**
 * @brief ZIP64 End of Central Directory Locator (20 bytes fixed)
 */
typedef struct {
    uint32_t signature;              /**< 0x07064B50 (PK\x06\x07) */
    uint32_t disk_with_zip64_eocd;
    uint64_t zip64_eocd_offset;
    uint32_t total_disks;
} xx_zip_zip64_locator_t;

/**
 * @brief ZIP64 End of Central Directory Record (56 bytes fixed)
 */
typedef struct {
    uint32_t signature;              /**< 0x06064B50 (PK\x06\x06) */
    uint64_t record_size;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint32_t disk_number;
    uint32_t cd_start_disk;
    uint64_t records_on_disk;
    uint64_t total_records;
    uint64_t cd_size;
    uint64_t cd_offset;
} xx_zip_zip64_eocd_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* XX_ZIP_DEFS_H */
