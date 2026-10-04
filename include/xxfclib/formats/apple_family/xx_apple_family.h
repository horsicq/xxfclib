/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared native Apple-family result state. Per-operation options are borrowed
 * only during synchronous iterator creation. Profile 0 means automatic.
 */
#ifndef XX_APPLE_FAMILY_INFO_H
#define XX_APPLE_FAMILY_INFO_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_apple_family_info {
    Abstractformat format;
    const xx_list_s *parse_options;
    uint64_t number_of_records;
    uint32_t profile, detected_profile;
    bool incomplete;
    const char *note;
    uint32_t cylinders,heads,sectors_per_track,sector_size;
} xx_apple_family_info;
#endif
