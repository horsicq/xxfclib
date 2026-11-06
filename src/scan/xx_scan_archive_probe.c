/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_archives_internal.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"

#include <string.h>

/* XBinary also types RIB and SPIS/TCompress streams FT_ARCHIVE, on these
 * header checks, where the common detector names them Binary. (A .tar.Z is
 * FT_ARCHIVE only once its decoded head is a TAR header, XTAR_COMPRESS, and
 * an SPIS stream also through XSPIS's full parse: neither is ported.) */
static bool xx_scan_archive_is_xbinary_archive(xx_io_device *device)
{
    uint8_t header[21];
    int64_t size = xx_io_total_size(device);
    uint32_t value;
    if (size < 8 || !xx_io_read_at(device, 0, header, size < 21 ? (size_t)size : 21U)) return false;
    if (size >= 8 && !memcmp(header, "RIB\0", 4)) {
        uint64_t packed = (uint64_t)size - 8U;
        value = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) | ((uint32_t)header[7] << 24);
        return packed <= value && value <= 512U * 1024U * 1024U && (packed == 0U) == (value == 0U);
    }
    if (size >= 21 && !memcmp(header, "SPIS\x1A", 5) &&
        (!memcmp(header + 5, "NON", 3) || !memcmp(header + 5, "RLE", 3) || !memcmp(header + 5, "LZH", 3) || !memcmp(header + 5, "CUS", 3) ||
         !memcmp(header + 5, "LH5", 3))) {
        value = (uint32_t)header[8] | ((uint32_t)header[9] << 8) | ((uint32_t)header[10] << 16) | ((uint32_t)header[11] << 24);
        return value > 0U && value <= 512U * 1024U * 1024U && header[12] <= 1U && !header[17] && !header[18] && !header[19] && !header[20];
    }
    return false;
}

/* Also true for the XBinary-only archives above: the engine's last check
 * for a member XFormats types FT_ARCHIVE but the common detector does not. */
bool xx_scan_archive_is_tar(xx_io_device *device)
{
    int64_t position = xx_io_tell(device);
    xx_tar tar;
    bool valid;
    if (xx_scan_archive_is_xbinary_archive(device)) {
        if (position >= 0) (void)xx_io_seek64(device, position, SEEK_SET);
        return true;
    }
    xx_tar_init(&tar, device, 0);
    valid = xx_tar_check_is_valid(&tar.format, NULL);
    xx_tar_destroy(&tar);
    if (position >= 0) (void)xx_io_seek64(device, position, SEEK_SET);
    return valid;
}

