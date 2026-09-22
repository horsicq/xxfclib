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

#ifndef XX_RAR_DEFS_H
#define XX_RAR_DEFS_H

#include <stdint.h>

#define XX_RAR_MAX_SFX_SIZE             (1024U * 1024U)
#define XX_RAR5_MAX_HEADER_SIZE         (2U * 1024U * 1024U)

#define XX_RAR4_SIGNATURE_SIZE          7U
#define XX_RAR5_SIGNATURE_SIZE          8U

#define XX_RAR4_HEADER_MAIN             0x73U
#define XX_RAR4_HEADER_FILE             0x74U
#define XX_RAR4_HEADER_SERVICE          0x7AU
#define XX_RAR4_HEADER_END              0x7BU
#define XX_RAR4_FLAG_LONG_BLOCK         0x8000U
#define XX_RAR4_MAIN_FLAG_PASSWORD      0x0080U
#define XX_RAR4_FILE_FLAG_SPLIT_BEFORE  0x0001U
#define XX_RAR4_FILE_FLAG_SPLIT_AFTER   0x0002U
#define XX_RAR4_FILE_FLAG_PASSWORD      0x0004U
#define XX_RAR4_FILE_FLAG_SOLID         0x0010U
#define XX_RAR4_FILE_FLAG_DIRECTORY     0x00E0U
#define XX_RAR4_FILE_FLAG_LARGE         0x0100U
#define XX_RAR4_FILE_FLAG_UNICODE       0x0200U
#define XX_RAR4_FILE_FLAG_SALT          0x0400U

#define XX_RAR5_HEADER_MAIN             1U
#define XX_RAR5_HEADER_FILE             2U
#define XX_RAR5_HEADER_SERVICE          3U
#define XX_RAR5_HEADER_CRYPT            4U
#define XX_RAR5_HEADER_END              5U
#define XX_RAR5_BLOCK_FLAG_EXTRA        0x0001U
#define XX_RAR5_BLOCK_FLAG_DATA         0x0002U
#define XX_RAR5_BLOCK_FLAG_SPLIT_BEFORE 0x0008U
#define XX_RAR5_BLOCK_FLAG_SPLIT_AFTER  0x0010U
#define XX_RAR5_FILE_FLAG_DIRECTORY     0x0001U
#define XX_RAR5_FILE_FLAG_MTIME         0x0002U
#define XX_RAR5_FILE_FLAG_CRC32         0x0004U
#define XX_RAR5_FILE_FLAG_UNKNOWN_SIZE  0x0008U
#define XX_RAR5_EXTRA_FILE_ENCRYPTION   0x0001U
#define XX_RAR5_EXTRA_FILE_HASH         0x0002U

#define XX_RAR5_COMP_VERSION_MASK       0x003FU
#define XX_RAR5_COMP_SOLID              0x0040U
#define XX_RAR5_COMP_METHOD_SHIFT       7U
#define XX_RAR5_COMP_DICTIONARY_SHIFT   10U
#define XX_RAR5_COMP_DICTIONARY_MASK    0x001FU

#pragma pack(push, 1)

typedef struct xx_rar4_base_header_s {
    uint16_t header_crc;
    uint8_t  header_type;
    uint16_t header_flags;
    uint16_t header_size;
} xx_rar4_base_header;

typedef struct xx_rar4_file_header_s {
    uint16_t header_crc;
    uint8_t  header_type;
    uint16_t header_flags;
    uint16_t header_size;
    uint32_t packed_size;
    uint32_t unpacked_size;
    uint8_t  host_os;
    uint32_t file_crc;
    uint32_t file_time;
    uint8_t  unpack_version;
    uint8_t  method;
    uint16_t name_size;
    uint32_t attributes;
} xx_rar4_file_header;

#pragma pack(pop)

#endif /* XX_RAR_DEFS_H */
