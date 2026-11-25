/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_FREEARC_SFX_LOCATOR_H
#define XFILEUNPACKER_FREEARC_SFX_LOCATOR_H
#include <stdint.h>
#include <string.h>
#include <xxfclib/io/xx_io.h>

static inline uint32_t fs_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint64_t fs_le64(const unsigned char *p)
{
    return (uint64_t)fs_le32(p) | (uint64_t)fs_le32(p + 4) << 32;
}
static inline bool fs_read_at(xx_io_device *source, uint64_t position, void *data, size_t size)
{
    size_t done = 0;
    if (position > INT64_MAX || xx_io_seek64(source, (int64_t)position, SEEK_SET)) return false;
    while (done < size) {
        ssize_t count = xx_io_read(source, (unsigned char *)data + done, size - done);
        if (count <= 0 || (size_t)count > size - done) return false;
        done += (size_t)count;
    }
    return true;
}

/* A bounded scan outside every PE section, with an absolute locator offset.
 * The raw FreeArc header must follow the locator immediately. */
static inline int64_t fs_locate_payload(xx_io_device *source)
{
    unsigned char header[64], section[40], window[65536 + 31];
    static const unsigned char magic[16] = "XFUFreeArcSFX01";
    uint64_t size, section_table, image_end = 0, end, at;
    uint32_t pe;
    unsigned sections, optional, i;
    int64_t total = xx_io_total_size(source), saved = xx_io_tell(source), result = -1;
    if (saved < 0 || total < 1024 || !fs_read_at(source, 0, header, sizeof(header)) || memcmp(header, "MZ", 2)) goto done;
    size = (uint64_t)total;
    pe = fs_le32(header + 60);
    if (pe > size - 24 || !fs_read_at(source, pe, header, 24) || memcmp(header, "PE\0\0", 4)) goto done;
    sections = (unsigned)header[6] | (unsigned)header[7] << 8;
    optional = (unsigned)header[20] | (unsigned)header[21] << 8;
    if (!sections || sections > 96 || optional < 64) goto done;
    section_table = (uint64_t)pe + 24 + optional;
    if (section_table > size || (uint64_t)sections * 40 > size - section_table) goto done;
    for (i = 0; i < sections; ++i) {
        uint64_t raw_end;
        if (!fs_read_at(source, section_table + (uint64_t)i * 40, section, sizeof(section))) goto done;
        raw_end = (uint64_t)fs_le32(section + 20) + fs_le32(section + 16);
        if (raw_end > size) goto done;
        if (raw_end > image_end) image_end = raw_end;
    }
    if (image_end < section_table + (uint64_t)sections * 40 || image_end > size - 32) goto done;
    end = image_end + 1048576U + 32U;
    if (end > size) end = size;
    for (at = image_end; at + 32 <= end; at += 65536U) {
        size_t length = end - at > sizeof(window) ? sizeof(window) : (size_t)(end - at), offset;
        if (!fs_read_at(source, at, window, length)) goto done;
        for (offset = 0; offset + 32 <= length && offset < 65536U; ++offset) {
            const unsigned char *candidate = window + offset;
            if (!memcmp(candidate, magic, 16) && fs_le64(candidate + 16) == at + offset + 24 && !memcmp(candidate + 24, "ArC\1", 4)) {
                result = (int64_t)(at + offset + 24);
                goto done;
            }
        }
    }
done:
    if (saved >= 0 && xx_io_seek64(source, saved, SEEK_SET)) result = -1;
    return result;
}
#endif
