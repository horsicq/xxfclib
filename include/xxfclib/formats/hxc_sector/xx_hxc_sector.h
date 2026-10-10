/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared result information for independently implemented HxC image readers.
 */
#ifndef XX_HXC_SECTOR_INFO_H
#define XX_HXC_SECTOR_INFO_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_hxc_sector_info_s {
    Abstractformat format;
    uint32_t cylinders, heads, sectors_per_track, sector_size;
    uint64_t number_of_records;
    /* Available components are valid, but a complete disk needs absent OS data. */
    bool incomplete;
    const char *note;
    /* Iterator-scoped options; never retained beyond creation. */
    const xx_list_s *parse_options;
} xx_hxc_sector_info;
#endif
