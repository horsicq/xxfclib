/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_TAR_DEFS_H
#define XXFCLIB_FORMAT_TAR_DEFS_H

#define XX_TAR_BLOCK_SIZE 512
#define XX_TAR_END_SIZE 1024
#define XX_TAR_MAX_NAME_SIZE (1024U * 1024U)
#define XX_TAR_MAX_MEMBERS 1000000U

#pragma pack(push, 1)
typedef struct xx_tar_header_s {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} xx_tar_header;
#pragma pack(pop)

#endif /* XXFCLIB_FORMAT_TAR_DEFS_H */
