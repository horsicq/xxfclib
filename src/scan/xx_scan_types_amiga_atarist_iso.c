/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/atarist/xx_atarist.h"

xx_file_type_t xx_scan_type_detect_amiga_atarist(xx_io_device *device, const uint8_t *head, size_t head_size)
{
    if (head_size >= 4U && (!memcmp(head, "\x00\x00\x03\xF3", 4) || !memcmp(head, "\x00\x00\x03\xE7", 4)))
        return XX_FILE_TYPE_AMIGAHUNK;
    if (head_size >= 2U && head[0] == 0x60U && head[1] == 0x1aU) {
        xx_atarist atari;
        bool valid;
        xx_atarist_init(&atari, device, 0);
        valid = xx_atarist_check_is_valid(&atari.format, NULL);
        xx_atarist_destroy(&atari);
        if (valid) return XX_FILE_TYPE_ATARIST;
    }
    return XX_FILE_TYPE_UNKNOWN;
}

bool xx_scan_type_is_iso9660(xx_io_device *device, int64_t size)
{
    uint8_t signature[5];
    return (size < 0 || size >= 0x8006) && xx_scan_type_read_at(device, 0x8001, signature, sizeof(signature)) && !memcmp(signature, "CD001", 5U);
}
