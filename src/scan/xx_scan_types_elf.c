/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"

xx_file_type_t xx_scan_type_detect_elf(const uint8_t *head, size_t head_size)
{
    if (head_size < 16U || memcmp(head, "\x7f" "ELF", 4)) return XX_FILE_TYPE_UNKNOWN;
    if (head[4] == 1U) return XX_FILE_TYPE_ELF32;
    if (head[4] == 2U) return XX_FILE_TYPE_ELF64;
    return XX_FILE_TYPE_UNKNOWN;
}
