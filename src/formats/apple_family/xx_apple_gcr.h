/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native Apple II 5&3 / 6&2 sector decoder. Published field,
 * packing and skew facts: Beneath Apple DOS; CiderPress SectorCodec notes.
 * Conflicting valid copies are rejected; missing sectors are never padded.
 */
#ifndef XX_APPLE_GCR_PRIVATE_H
#define XX_APPLE_GCR_PRIVATE_H
#include "xx_apple_volumes.h"
#include "xxfclib/data/xx_data.h"
static const uint8_t ag_codes62[64] = {0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba,
                                       0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3, 0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7,
                                       0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
static const uint8_t ag_codes53[32] = {0xab, 0xad, 0xae, 0xaf, 0xb5, 0xb6, 0xb7, 0xba, 0xbb, 0xbd, 0xbe, 0xbf, 0xd6, 0xd7, 0xda, 0xdb,
                                       0xdd, 0xde, 0xdf, 0xea, 0xeb, 0xed, 0xee, 0xef, 0xf5, 0xf6, 0xf7, 0xfa, 0xfb, 0xfd, 0xfe, 0xff};
static const uint8_t ag_phys_dos[16] = {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};
static uint8_t ag_byte(const uint8_t *p, uint32_t n, uint32_t at)
{
    return p[at % n];
}
static bool ag_four(const uint8_t *p, uint32_t n, uint32_t at, uint8_t *out)
{
    uint8_t a = ag_byte(p, n, at), b = ag_byte(p, n, at + 1U);
    if ((a & 0xaaU) != 0xaaU || (b & 0xaaU) != 0xaaU) {
        return false;
    }
    *out = (uint8_t)(((a << 1U) | 1U) & b);
    return true;
}
static bool ag_data(const uint8_t *p, uint32_t n, uint32_t at, unsigned sectors, uint8_t *out)
{
    uint8_t inverse[256], values[410], previous = 0;
    uint32_t i, count = sectors == 13U ? 410U : 342U;
    xx_rt_memset(inverse, 255, sizeof(inverse));
    for (i = 0; i < (sectors == 13U ? 32U : 64U); ++i) inverse[sectors == 13U ? ag_codes53[i] : ag_codes62[i]] = (uint8_t)i;
    for (i = 0; i < count; ++i) {
        uint8_t z = inverse[ag_byte(p, n, at + i)];
        if (z == 255U) return false;
        previous ^= z;
        values[i] = previous;
    }
    if (inverse[ag_byte(p, n, at + count)] != previous || ag_byte(p, n, at + count + 1U) != 0xdeU || ag_byte(p, n, at + count + 2U) != 0xaaU) return false;
    if (sectors == 16U) {
        for (i = 0; i < 256U; ++i) {
            uint8_t z = (values[i % 86U] >> (2U * (i / 86U))) & 3U;
            out[i] = (uint8_t)((values[86U + i] << 2U) | ((z & 1U) << 1U) | ((z & 2U) >> 1U));
        }
    } else {
        uint8_t low[154];
        for (i = 0; i < 154U; ++i) low[153U - i] = values[i];
        for (i = 0; i < 255U; ++i) {
            uint32_t column = 50U - i / 5U, row = i % 5U;
            uint8_t z;
            if (row < 3U) z = (low[column + row * 51U] >> 2U) & 7U;
            else {
                unsigned bit = row == 3U ? 1U : 0U;
                z = (uint8_t)((((low[column] >> bit) & 1U) << 2U) | (((low[column + 51U] >> bit) & 1U) << 1U) | ((low[column + 102U] >> bit) & 1U));
            }
            out[i] = (uint8_t)((values[154U + column + row * 51U] << 3U) | z);
        }
        out[255] = (uint8_t)((values[409] << 3U) | (low[153] & 7U));
    }
    return true;
}
/* Returns the number of independently authenticated sectors. Whole images
 * require the exact complete mask; a partial capture remains a track component. */
static int ag_track(af_work *w, const uint8_t *p, uint32_t n, uint32_t track, uint8_t *image, unsigned *sectors)
{
    uint32_t i, mask = 0;
    unsigned detected = 0;
    uint8_t volume = 0;
    bool have_volume = false;
    if (n < 512U || n > 131072U) return -1;
    for (i = 0; i < n; ++i) {
        uint8_t v, t, sector, check, out[256];
        uint32_t gap;
        unsigned count;
        if (!(i & 255U) && !af_poll(w)) return -1;
        if (ag_byte(p, n, i) != 0xd5U || ag_byte(p, n, i + 1U) != 0xaaU) continue;
        if (ag_byte(p, n, i + 2U) == 0x96U) count = 16U;
        else if (ag_byte(p, n, i + 2U) == 0xb5U) count = 13U;
        else continue;
        if (!ag_four(p, n, i + 3U, &v) || !ag_four(p, n, i + 5U, &t) || !ag_four(p, n, i + 7U, &sector) || !ag_four(p, n, i + 9U, &check) || t != track ||
            sector >= count || (uint8_t)(v ^ t ^ sector) != check || ag_byte(p, n, i + 11U) != 0xdeU || ag_byte(p, n, i + 12U) != 0xaaU)
            continue;
        for (gap = 14U; gap < 270U; ++gap) {
            uint32_t data = i + gap;
            if (ag_byte(p, n, data) != 0xd5U || ag_byte(p, n, data + 1U) != 0xaaU) continue;
            if (ag_byte(p, n, data + 2U) == 0x96U || ag_byte(p, n, data + 2U) == 0xb5U) break;
            if (ag_byte(p, n, data + 2U) != 0xadU) continue;
            if (ag_data(p, n, data + 3U, count, out)) {
                uint32_t logical = count == 16U ? ag_phys_dos[sector] : sector;
                uint8_t *target = image + logical * 256U;
                if ((detected && detected != count) || (have_volume && volume != v) || (mask & (1U << sector) && xx_rt_memcmp(target, out, 256))) return -1;
                xx_rt_memcpy(target, out, 256);
                mask |= 1U << sector;
                detected = count;
                volume = v;
                have_volume = true;
            }
            break;
        }
    }
    *sectors = detected;
    {
        int found = 0;
        while (mask) {
            found += (int)(mask & 1U);
            mask >>= 1U;
        }
        return found;
    }
}
/* Bitstream latch model: sync zeros are skipped until the next set bit,
 * then the next eight cells form one high-bit disk nibble. One extra cycle
 * permits fields that cross the stored track boundary. */
static XXFC_MAYBE_UNUSED uint32_t ag_latch(af_work *w, const uint8_t *bits, uint32_t count, uint8_t *out, uint32_t capacity)
{
    uint32_t at = 0, used = 0, start = UINT32_MAX;
    unsigned value = 0, len = 0;
    if (!count || count > 1048576U) return 0;
    while (at < count * 2U + 8U) {
        unsigned bit;
        if (!len && ((start == UINT32_MAX && at >= count) || (start != UINT32_MAX && at >= start + count))) break;
        bit = (bits[(at % count) / 8U] >> (7U - ((at % count) & 7U))) & 1U;
        if (!(at & 4095U) && !af_poll(w)) {
            return 0;
        }
        if (start == UINT32_MAX && bit) start = at;
        ++at;
        if (!len && !bit) continue;
        value = (value << 1U) | bit;
        if (++len == 8U) {
            if (used >= capacity) return 0;
            out[used++] = (uint8_t)value;
            len = 0;
            value = 0;
        }
    }
    return used;
}
static bool ag_files(af_work *w, uint8_t *image, uint32_t tracks, unsigned sectors, bool *recognized)
{
    xx_io_device *d;
    bool ok = true;
    const uint8_t *vtoc = image + 17U * sectors * 256U;
    unsigned kind = 0;
    uint32_t size = tracks * sectors * 256U;
    *recognized = false;
    if (vtoc[1] == 17U && vtoc[2] && vtoc[2] < sectors && vtoc[0x34] == tracks && vtoc[0x35] == sectors && xx_data_get_u16(vtoc + 0x36, 2, 0, false) == 256U)
        kind = sectors == 13U ? 6U : 4U;
    else if (sectors == 16U) {
        const uint8_t *h = image + 2816U;
        if ((h[4] & 0xf0U) == 0xf0U && (h[4] & 15U) && h[35] == 39U && h[36] == 13U) kind = 1;
        else if (xx_data_get_u16(h, 2, 0, false) == 0U && xx_data_get_u16(h + 2, 2, 0, false) == 6U && xx_data_get_u16(h + 4, 2, 0, false) == 0U && h[6] >= 1U &&
                 h[6] <= 7U)
            kind = 2;
    }
    if (!kind) {
        return true;
    }
    *recognized = true;
    d = xx_io_mem_open_ro(image, size);
    if (!d) return false;
    ok = av_volume(w, d, "FILES", kind);
    xx_io_close(d);
    return ok;
}
#endif
