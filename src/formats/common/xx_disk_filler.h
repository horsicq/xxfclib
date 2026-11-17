/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared tolerance test for raw disk-image containers whose size check is
 * exact (SaveDskF, COPYDISK, DiskDupe). Images copied through record-based
 * media or written by a tool that stopped mid-sector differ from the
 * geometry by less than one sector or track; that difference is accepted
 * only when the bytes involved are one repeated filler value: 0x00, 0x1A
 * (CP/M and DOS EOF padding), 0x20 (space padding) or 0xF6 (the DOS format
 * fill byte). Anything else is still a size mismatch. */
#ifndef XX_DISK_FILLER_PRIVATE_H
#define XX_DISK_FILLER_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool xx_disk_filler_byte(uint8_t value)
{
    return value == 0x00U || value == 0x1aU || value == 0x20U || value == 0xf6U;
}

/* True when size > 0 and every byte is the same filler value. */
static inline bool xx_disk_filler_run(const uint8_t *bytes, size_t size)
{
    size_t i;

    if (!bytes || size == 0U || !xx_disk_filler_byte(bytes[0])) return false;
    for (i = 1U; i < size; ++i) {
        if (bytes[i] != bytes[0]) return false;
    }
    return true;
}

#endif
