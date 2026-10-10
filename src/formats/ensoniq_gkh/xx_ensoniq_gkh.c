/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * GKH Intel/version1 tag facts: HxC gkh_format.h and its bundled Ensoniq
 * file-format description. Original implementation; no producer code used.
 * Exports the declared raw image and bounded author/subject components.
 */
#include "xxfclib/formats/ensoniq_gkh/xx_ensoniq_gkh.h"
#include "../xx_hxc_sector.h"
#include "xxfclib/data/xx_data.h"
static bool gkh_parse(Abstractformat *f, pm_stream *s, hc_blob *b)
{
    xx_ensoniq_gkh *r = (xx_ensoniq_gkh *)f;
    uint32_t count, table, i, seen = 0U, at[3] = {0}, size[3] = {0}, expected = 0U, ranges = 0U;
    hc_span spans[4];
    if (!hc_span_ok(b, 0U, 8U) || xx_rt_memcmp(b->p, "TDDFI\1", 6U)) return false;
    count = xx_data_get_u16(b->p + 6U, 2, 0, false);
    if (count < 3U || count > 5U || !hc_span_ok(b, 8U, count * 10U)) return false;
    table = 8U + count * 10U;
    if (!hc_disjoint(spans, &ranges, 4U, 0U, table)) return false;
    for (i = 0U; i < count; ++i) {
        const uint8_t *tag = b->p + 8U + i * 10U;
        unsigned bit, slot;
        if (!hc_poll(b)) return false;
        switch (tag[0]) {
            case 1U:
                bit = 1U;
                if (tag[1] != 4U || xx_data_get_u32(tag + 2U, 4, 0, false) != 1U || xx_data_get_u32(tag + 6U, 4, 0, false) != 1U) return false;
                break;
            case 10U:
                bit = 2U;
                if (tag[1] != 5U || !hc_geometry(r, xx_data_get_u16(tag + 2U, 2, 0, false), xx_data_get_u16(tag + 4U, 2, 0, false),
                                                 xx_data_get_u16(tag + 6U, 2, 0, false), xx_data_get_u16(tag + 8U, 2, 0, false), &expected))
                    return false;
                break;
            case 11U:
            case 20U:
            case 21U:
                bit = tag[0] == 11U ? 4U : tag[0] == 20U ? 8U : 16U;
                slot = tag[0] == 11U ? 0U : tag[0] == 20U ? 1U : 2U;
                if (tag[1] != (tag[0] == 11U ? 11U : 10U)) return false;
                size[slot] = xx_data_get_u32(tag + 2U, 4, 0, false);
                at[slot] = xx_data_get_u32(tag + 6U, 4, 0, false);
                if (at[slot] < table || !hc_span_ok(b, at[slot], size[slot]) || !hc_disjoint(spans, &ranges, 4U, at[slot], size[slot])) return false;
                break;
            default: return false;
        }
        if (seen & bit) return false;
        seen |= bit;
    }
    if ((seen & 7U) != 7U || size[0] != expected || !hc_emit(f, s, b, "disk.img", at[0], size[0])) return false;
    if ((seen & 8U) && !hc_emit(f, s, b, "author.txt", at[1], size[1])) return false;
    if ((seen & 16U) && !hc_emit(f, s, b, "subject.txt", at[2], size[2])) return false;
    r->note = "tag-declared raw sector image; no filesystem conversion";
    return true;
}
HC_PARSE_WRAPPER(gkh_parse)
HC_DEFINE_READER(ensoniq_gkh, XX_FILE_TYPE_ENSONIQ_GKH, "gkh")
