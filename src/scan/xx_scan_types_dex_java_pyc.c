/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/pyc/xx_pyc.h"

bool xx_scan_type_is_java_class(const uint8_t *head, size_t head_size)
{
    return head_size >= 24U && !memcmp(head, "\xCA\xFE\xBA\xBE", 4) && (((uint32_t)head[6] << 8U) | head[7]) >= 45U;
}

xx_file_type_t xx_scan_type_detect_dex(const uint8_t *head, size_t head_size)
{
    return head_size >= 8U && !memcmp(head, "dex\n", 4) && head[7] == 0U ? XX_FILE_TYPE_DEX : XX_FILE_TYPE_UNKNOWN;
}

xx_file_type_t xx_scan_type_detect_pyc(const uint8_t *head, size_t head_size)
{
    return head_size >= 4U && head[2] == 0x0dU && head[3] == 0x0aU &&
                   xx_pyc_is_known_magic((uint16_t)head[0] | ((uint16_t)head[1] << 8U)) ?
               XX_FILE_TYPE_PYC : XX_FILE_TYPE_UNKNOWN;
}
