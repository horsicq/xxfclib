/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * PC99 token tracks:3253 bytes FM or6872 bytes MFM,40 cylinders,1/2 sides
 * stored side-sequential. Validate all9/18 distinct256-byte sector frames.
 * Stored CRC16 is checked; F7 F7 means controller-generated CRC and is
 * preserved as such. Export original token tracks, not synthetic CRC bytes.
 * Format facts: primary HxC ti99pc99_loader and TI disk controller framing.
 */
#include "xxfclib/formats/ti99_pc99/xx_ti99_pc99.h"
#include "../xx_hxc_sector.h"
#include "xxfclib/data/xx_data.h"
static bool pc_crc(const uint8_t *p, uint32_t n, const uint8_t *stored) {
    return (stored[0] == 0xF7U && stored[1] == 0xF7U) || hc_crc16(p, n, 0xFFFFU) == xx_data_get_u16(stored, 2, 0, true);
}
static bool pc_track(hc_blob *b, const uint8_t *p, uint32_t n, uint32_t cylinder, uint32_t head, bool fm) {
    uint32_t i = 0U, seen = 0U, wanted = fm ? 9U : 18U;
    while (i + 7U < n) {
        uint32_t prefix, search, data, sector, mask;
        if (!(i & 255U) && !hc_poll(b)) return false;
        if (p[i] != 0xFEU || (!fm && (i < 3U || p[i - 3U] != 0xA1U || p[i - 2U] != 0xA1U || p[i - 1U] != 0xA1U))) { ++i; continue; }
        prefix = fm ? i : i - 3U;
        if (p[i + 1U] != cylinder || p[i + 2U] != head || p[i + 3U] >= wanted || p[i + 4U] != 1U ||
            !pc_crc(p + prefix, i + 5U - prefix, p + i + 5U)) { ++i; continue; }
        sector = p[i + 3U]; mask = 1U << sector;
        for (search = i + 7U; search < n && search - i < 96U; ++search) {
            if ((p[search] == 0xFBU || p[search] == 0xF8U) &&
                (fm || (search >= 3U && p[search - 3U] == 0xA1U && p[search - 2U] == 0xA1U && p[search - 1U] == 0xA1U))) break;
            if (p[search] == 0xFEU) return false;
        }
        if (search >= n || search - i >= 96U || n - search < 259U || (seen & mask)) return false;
        data = fm ? search : search - 3U;
        if (!pc_crc(p + data, search + 257U - data, p + search + 257U)) return false;
        seen |= mask; i = search + 259U;
    }
    return hc_poll(b) && seen == (1U << wanted) - 1U;
}
static bool pc_parse(Abstractformat *f, pm_stream *s, hc_blob *b) {
    xx_ti99_pc99 *r = (xx_ti99_pc99 *)f; uint32_t size, tracks, heads, i; bool fm; char name[48];
    if (b->n == 40U * 3253U || b->n == 80U * 3253U) { size = 3253U; fm = true; }
    else if (b->n == 40U * 6872U || b->n == 80U * 6872U) { size = 6872U; fm = false; }
    else return false;
    tracks = b->n / size; heads = tracks / 40U;
    for (i = 0U; i < tracks; ++i) {
        if (!pc_track(b, b->p + i * size, size, i % 40U, i / 40U, fm)) return false;
        xx_rt_snprintf(name, sizeof(name), "track-%03u-head-%u.tokens", i % 40U, i / 40U);
        if (!hc_emit(f, s, b, name, i * size, size)) return false;
    }
    r->cylinders = 40U; r->heads = heads; r->sectors_per_track = fm ? 9U : 18U; r->sector_size = 256U;
    r->note = "original controller-token tracks; F7 F7 CRC placeholders are preserved; no filesystem conversion";
    return true;
}
HC_PARSE_WRAPPER(pc_parse)
HC_DEFINE_READER(ti99_pc99, XX_FILE_TYPE_TI99_PC99, "pc99")
