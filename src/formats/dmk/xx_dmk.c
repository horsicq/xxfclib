/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original implementation; primary producer references are in xx_dmk.h.
 */
#include "xxfclib/formats/dmk/xx_dmk.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define DC_MAX_MEMBERS 512U
#define DC_NATIVE_DMK 1
#include "../wux/xx_disk_containers_native.h"

#ifdef DMK
#define DC_FILE_TYPE XX_FILE_TYPE_DMK
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define DMK_MFM UINT32_C(0x40000000)
#define DMK_DOUBLED UINT32_C(0x80000000)
#define DMK_OFFSET UINT32_C(0x3fffffff)

typedef struct dmk_entry {
    uint32_t encoded;
    uint32_t size;
    uint8_t sector;
} dmk_entry;

static uint16_t dmk_crc_byte(uint16_t crc, uint8_t byte) {
    return xx_crc16_ccitt_calc(crc, &byte, 1U);
}
static uint16_t dmk_crc_start(bool mfm) {
    uint16_t crc = UINT16_C(0xffff);
    if (mfm) {
        crc = dmk_crc_byte(crc, UINT8_C(0xa1));
        crc = dmk_crc_byte(crc, UINT8_C(0xa1));
        crc = dmk_crc_byte(crc, UINT8_C(0xa1));
    }
    return crc;
}
static bool dmk_bytes(const uint8_t *track, size_t length, size_t at,
                       size_t count, unsigned step) {
    size_t i;
    if (at > length || count > (length - at) / step) return false;
    if (step == 2U)
        for (i = 0U; i < count; ++i)
            if (track[at + i * 2U] != track[at + i * 2U + 1U]) return false;
    return true;
}
static bool dmk_data_crc(const uint8_t *data, uint32_t size, unsigned step,
                         bool mfm, uint8_t *plain) {
    uint16_t crc = dmk_crc_start(mfm);
    size_t i;
    uint8_t mark = data[0];
    if ((mfm && mark != 0xfbU && mark != 0xf8U) ||
        (!mfm && (mark < 0xf8U || mark > 0xfbU))) return false;
    for (i = 0U; i < (size_t)size + 3U; ++i) {
        uint8_t byte = data[i * step];
        if (step == 2U && byte != data[i * step + 1U]) return false;
        crc = dmk_crc_byte(crc, byte);
        if (plain && i >= 1U && i <= size) plain[i - 1U] = byte;
    }
    return crc == 0U;
}

/* Revalidate every data CRC while reconstructing a listed image. */
static bool dc_dmk_sector(Abstractformat *f, const dc_image *image,
                          uint32_t encoded, uint8_t *plain, xx_pd_struct *pd) {
    uint8_t raw[16390];
    bool mfm = (encoded & DMK_MFM) != 0U;
    unsigned step = (encoded & DMK_DOUBLED) ? 2U : 1U;
    uint32_t payload = encoded & DMK_OFFSET;
    size_t before = mfm ? 4U : step;
    size_t count;
    if (image->block_size < 128U || image->block_size > 8192U ||
        payload < before || (mfm && step != 1U)) return false;
    count = ((size_t)image->block_size + 3U) * step + (mfm ? 3U : 0U);
    if (!dc_read(f, image, payload - before, raw, count, pd) ||
        (mfm && (raw[0] != 0xa1U || raw[1] != 0xa1U || raw[2] != 0xa1U))) return false;
    return dmk_data_crc(raw + (mfm ? 3U : 0U), image->block_size, step, mfm, plain);
}

static bool dmk_track(const uint8_t *track, uint32_t length, uint32_t absolute,
    unsigned cylinder, unsigned head, uint8_t flags, dmk_entry *entries,
    unsigned *count, unsigned *first_sector, uint32_t *sector_size) {
    uint16_t pointers[64];
    unsigned n = 0U, i, min = 255U, max = 0U;
    uint8_t seen[256] = {0};
    bool ended = false;
    for (i = 0U; i < 64U; ++i) {
        uint16_t pointer = dc_le16(track + i * 2U);
        uint16_t offset = (uint16_t)(pointer & UINT16_C(0x3fff));
        if (!pointer) { ended = true; continue; }
        if (ended || (pointer & UINT16_C(0x4000)) || offset < 128U ||
            offset >= length || (n && offset <= (pointers[n - 1U] & UINT16_C(0x3fff)))) return false;
        pointers[n++] = pointer;
    }
    if (!n) return false;
    for (i = 0U; i < n; ++i) {
        bool mfm = (pointers[i] & UINT16_C(0x8000)) != 0U;
        unsigned step = mfm || (flags & 0x40U) ? 1U : 2U;
        size_t at = pointers[i] & UINT16_C(0x3fff);
        size_t end = i + 1U < n ? pointers[i + 1U] & UINT16_C(0x3fff) : length;
        size_t data, j, limit;
        uint16_t crc = dmk_crc_start(mfm);
        unsigned id, size_code;
        uint32_t size;
        bool found = false;
        if (i + 1U < n && (pointers[i + 1U] & UINT16_C(0x8000))) {
            if (end < 131U) return false;
            end -= 3U;
        }
        if (!dmk_bytes(track, end, at, 7U, step) || track[at] != 0xfeU ||
            track[at + step] != cylinder || track[at + step * 2U] != head ||
            (mfm && (at < 131U || track[at - 3U] != 0xa1U ||
                       track[at - 2U] != 0xa1U || track[at - 1U] != 0xa1U))) return false;
        for (j = 0U; j < 7U; ++j) crc = dmk_crc_byte(crc, track[at + j * step]);
        if (crc) return false;
        id = track[at + step * 3U]; size_code = track[at + step * 4U];
        if (seen[id] || size_code > 6U) return false;
        seen[id] = 1U;
        if (id < min) min = id;
        if (id > max) max = id;
        size = UINT32_C(128) << size_code;
        if (i && size != entries[0].size) return false;
        data = at + 7U * step;
        /* WD179x's bounded search after the ID CRC: 43 bytes for MFM,
         * 30 logical bytes for FM. Require MFM's physical A1 sync prefix. */
        limit = mfm ? 43U : 30U;
        for (j = 0U; j < limit; ++j, data += step) {
            uint8_t mark;
            if (data >= end) break;
            mark = track[data];
            if ((mfm && mark != 0xf8U && mark != 0xfbU) ||
                (!mfm && (mark < 0xf8U || mark > 0xfbU))) continue;
            if (mfm && (data < 131U || track[data - 3U] != 0xa1U ||
                track[data - 2U] != 0xa1U || track[data - 1U] != 0xa1U)) continue;
            if (!dmk_bytes(track, end, data, (size_t)size + 3U, step) ||
                !dmk_data_crc(track + data, size, step, mfm, NULL)) return false;
            found = true; break;
        }
        if (!found) return false;
        entries[i].sector = (uint8_t)id; entries[i].size = size;
        entries[i].encoded = absolute + (uint32_t)data + step;
        if (mfm) entries[i].encoded |= DMK_MFM;
        if (step == 2U) entries[i].encoded |= DMK_DOUBLED;
    }
    if (min > 1U || max - min + 1U != n) return false;
    *count = n; *first_sector = min; *sector_size = entries[0].size;
    return true;
}

static bool dc_parse(Abstractformat *f, dc_image *image,
                      const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[16], *track = NULL;
    dmk_entry entries[64];
    uint32_t length, cylinders, heads, tracks, t;
    unsigned sectors = 0U, first = 0U;
    uint64_t physical;
    bool valid = false;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) ||
        (header[0] != 0U && header[0] != 0xffU) || !header[1] ||
        (header[4] & 0xafU) || !dc_zero(header + 5U, 11U)) return false;
    length = dc_le16(header + 2U); cylinders = header[1];
    heads = (header[4] & 0x10U) ? 1U : 2U; tracks = cylinders * heads;
    physical = (uint64_t)tracks * length;
    if (length <= 128U || length >= 16384U ||
        !dc_span(sizeof(header), physical, image->available) ||
        !dc_memory_limit(f, options, sizeof(*image) + length)) return false;
    track = (uint8_t *)xx_mem_alloc(length);
    if (!track) return false;
    image->extent = sizeof(header) + physical;
    for (t = 0U; t < tracks; ++t) {
        uint32_t absolute = (uint32_t)sizeof(header) + t * length;
        uint32_t sector_size;
        unsigned n, start, i;
        char name[64];
        if (!dc_read(f, image, absolute, track, length, pd) ||
            !dmk_track(track, length, absolute, t / heads, t % heads,
                        header[4], entries, &n, &start, &sector_size)) goto done;
        if (!t) {
            sectors = n; first = start; image->block_size = sector_size;
            image->map_count = tracks * sectors;
            if (!dc_memory_limit(f, options, sizeof(*image) + length +
                  (uint64_t)image->map_count * sizeof(uint32_t))) goto done;
            image->map = (uint32_t *)xx_mem_alloc((size_t)image->map_count * sizeof(uint32_t));
            if (!image->map || !dc_add(image, "disk.img", sizeof(header),
                    (uint64_t)image->map_count * sector_size, physical, DC_DMK_SECTORS,
                    0U, sizeof(header)) ||
                !dc_add(image, "header.bin", 0U, sizeof(header), sizeof(header),
                          DC_STORED, 0U, sizeof(header))) goto done;
        } else if (n != sectors || start != first || sector_size != image->block_size) goto done;
        for (i = 0U; i < n; ++i)
            image->map[t * sectors + entries[i].sector - first] = entries[i].encoded;
        xx_rt_snprintf(name, sizeof(name), "track-%03u-side-%u.raw", t / heads, t % heads);
        if (!dc_add(image, name, absolute, length, length, DC_STORED,
                      absolute, 128U)) goto done;
    }
    valid = !dc_stopped(pd);
done:
    xx_mem_free(track);
    return valid;
}

XX_DC_IMPLEMENT(dmk, DC_FILE_TYPE, "dmk", "application/x-dmk-disk-image")
