/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_AR_DEFS_H
#define XXFCLIB_FORMAT_AR_DEFS_H

#define XX_AR_MAGIC "!<arch>\n"
#define XX_AR_THIN_MAGIC "!<thin>\n"
#define XX_AR_MAGIC_SIZE 8
#define XX_AR_MEMBER_HEADER_SIZE 60
#define XX_AR_MAX_NAME_SIZE (1024U * 1024U)

#pragma pack(push, 1)
typedef struct xx_ar_member_header_s {
    char name[16];
    char timestamp[12];
    char owner_id[6];
    char group_id[6];
    char mode[8];
    char size[10];
    char trailer[2];
} xx_ar_member_header;
#pragma pack(pop)

#endif /* XXFCLIB_FORMAT_AR_DEFS_H */
