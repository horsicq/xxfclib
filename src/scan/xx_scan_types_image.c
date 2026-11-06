/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"

xx_file_type_t xx_scan_type_detect_image(const uint8_t *head, size_t head_size)
{
    if (head_size >= 8U && !memcmp(head, "\x89PNG\r\n\x1a\n", 8)) return XX_FILE_TYPE_PNG;
    if (head_size >= 3U && head[0] == 0xffU && head[1] == 0xd8U && head[2] == 0xffU) return XX_FILE_TYPE_JPEG;
    return XX_FILE_TYPE_UNKNOWN;
}
