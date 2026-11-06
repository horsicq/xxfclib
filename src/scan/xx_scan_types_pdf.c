/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"

/* PDF scan detection accepts a header after a BOM or other leading bytes when
 * the complete "%PDF-" marker begins within the first 1024 bytes. */
bool xx_scan_type_is_pdf(xx_io_device *device, int64_t file_size, xx_pd_struct *pd)
{
    uint8_t bytes[1029];
    size_t limit;
    size_t done = 0;
    size_t index;
    if (file_size >= 0 && file_size <= 4) return false;
    limit = file_size >= 0 && file_size < (int64_t)sizeof(bytes) ? (size_t)file_size : sizeof(bytes);
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return false;
    while (done < limit) {
        ssize_t count;
        if (pd && pd->is_stop) return false;
        count = xx_io_read(device, bytes + done, limit - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    if (done < 5U) return false;
    if (!memcmp(bytes, "%PDF", 4U)) return true;
    for (index = 1U; index <= 1024U && index + 5U <= done; ++index) {
        if (!memcmp(bytes + index, "%PDF-", 5U)) return true;
    }
    return false;
}
