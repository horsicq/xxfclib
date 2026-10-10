/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Gericom/EveryFileExplorer/master/3DS/NintendoWare/FONT/CFNT.cs
 * CFNT version0x3000000, little-endian3DS CFNT. Exports FINF/CWDH/CMAP metadata, TGLP header and up to32 encoded sheets; validates linked tables, glyph indices and
 * disjoint framing. No texture conversion, BNTX sheets, text shaping or font rendering.
 */
#include "xxfclib/formats/nintendo_bcfnt/xx_nintendo_bcfnt.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint16_t g16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static uint32_t g32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f, uint64_t at, uint64_t end, bool empty)
{
    uint8_t c;
    uint64_t i;
    if (at >= end || end > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < 4096 && at + i < end; ++i) {
        if (!pm_read(f, (int64_t)(at + i), &c, 1)) return false;
        if (!c) return empty || i != 0;
    }
    return false;
}
static bool bom(const uint8_t *p, bool *be)
{
    *be = p[0] == 0xfe && p[1] == 0xff;
    return *be || (p[0] == 0xff && p[1] == 0xfe);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20], p[32], v[4];
    uint64_t offsets[1024], sizes[1024], at = 20, texture = 0;
    uint8_t kinds[1024];
    uint32_t total, count, i, j, finf = UINT32_MAX, tglp = UINT32_MAX, cwdh = UINT32_MAX, cmap = UINT32_MAX, refs[3], sheets = 0, sheet_size = 0, capacity = 0;
    bool be;
    if (!pm_read(f, 0, h, 20) || xx_rt_memcmp(h, "CFNT", 4) || !bom(h + 4, &be) || be != false || g16(h + 6, be) != 20 || g32(h + 8, be) != 0x3000000) return false;
    total = g32(h + 12, be);
    count = g32(h + 16, be);
    if (count < 4 || count > 1024 || total > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < count; ++i) {
        uint32_t n;
        uint8_t kind;
        if ((pd && xx_pd_is_stopped(pd)) || !span(at, 8, total) || !pm_read(f, (int64_t)at, p, 8)) {
            return false;
        }
        n = g32(p + 4, be);
        if (n < 8 || !span(at, n, total)) return false;
        if (!xx_rt_memcmp(p, "FINF", 4)) {
            if (finf != UINT32_MAX || i || n != 32) return false;
            kind = 0;
            finf = i;
        } else if (!xx_rt_memcmp(p, "TGLP", 4)) {
            if (tglp != UINT32_MAX || n < 32) return false;
            kind = 1;
            tglp = i;
        } else if (!xx_rt_memcmp(p, "CWDH", 4)) {
            if (n < 16) return false;
            kind = 2;
            if (cwdh == UINT32_MAX) cwdh = i;
        } else if (!xx_rt_memcmp(p, "CMAP", 4)) {
            if (n < 22) return false;
            kind = 3;
            if (cmap == UINT32_MAX) cmap = i;
        } else return false;
        offsets[i] = at;
        sizes[i] = n;
        kinds[i] = kind;
        at += n;
    }
    if (at != total || finf == UINT32_MAX || tglp == UINT32_MAX || cwdh == UINT32_MAX || cmap == UINT32_MAX || !pm_read(f, (int64_t)offsets[finf], p, 32) || p[8] != 1)
        return false;
    refs[0] = g32(p + 16, be);
    refs[1] = g32(p + 16 + 4, be);
    refs[2] = g32(p + 16 + 8, be);
    if (refs[0] != offsets[tglp] + 8 || refs[1] != offsets[cwdh] + 8 || refs[2] != offsets[cmap] + 8 || !pm_read(f, (int64_t)offsets[tglp], p, 32)) return false;
    sheets = g16(p + 16, be);
    sheet_size = g32(p + 12, be);
    texture = g32(p + 28, be);
    if (!p[8] || !p[9] || !sheets || sheets > 32 || !sheet_size || g16(p + 18, be) > 13 || !g16(p + 20, be) || !g16(p + 22, be) || !g16(p + 24, be) || !g16(p + 26, be) ||
        texture < offsets[tglp] + 32 || !span(texture - offsets[tglp], (uint64_t)sheets * sheet_size, sizes[tglp]))
        return false;
    capacity = (uint32_t)g16(p + 20, be) * g16(p + 22, be);
    if (capacity > 65535U / sheets) return false;
    capacity *= sheets;
    for (i = 0; i < count; ++i) {
        char label[40];
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (kinds[i] == 2 || kinds[i] == 3) {
            uint32_t begin, end, next, k, expected_next = 0;
            if (!pm_read(f, (int64_t)offsets[i], p, kinds[i] == 2 ? 16 : 20)) return false;
            begin = g16(p + 8, be);
            end = g16(p + 10, be);
            next = g32(p + (kinds[i] == 2 ? 12 : 16), be);
            for (j = i + 1; j < count; ++j)
                if (kinds[j] == kinds[i]) {
                    expected_next = (uint32_t)offsets[j] + 8;
                    break;
                }
            if (begin > end || next != expected_next) return false;
            if (kinds[i] == 2) {
                if (end >= capacity || !span(16, 3U * (uint64_t)(end - begin + 1), sizes[i])) return false;
            } else {
                uint32_t method = g16(p + 12, be);
                if (method > 2 || g16(p + 14, be)) return false;
                if (method == 0) {
                    if (!pm_read(f, (int64_t)offsets[i] + 20, v, 2) || (uint64_t)g16(v, be) + end - begin >= capacity) return false;
                } else if (method == 1) {
                    if (!span(20, 2U * (uint64_t)(end - begin + 1), sizes[i])) return false;
                    for (k = 0; k <= end - begin; ++k) {
                        uint16_t glyph;
                        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)offsets[i] + 20 + k * 2, v, 2)) return false;
                        glyph = g16(v, be);
                        if (glyph != 65535 && glyph >= capacity) return false;
                    }
                } else {
                    uint32_t entries, previous = 0;
                    if (!pm_read(f, (int64_t)offsets[i] + 20, v, 2)) {
                        return false;
                    }
                    entries = g16(v, be);
                    if (!span(22, (uint64_t)entries * 4, sizes[i])) return false;
                    for (k = 0; k < entries; ++k) {
                        uint32_t code;
                        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)offsets[i] + 22 + k * 4, v, 4)) return false;
                        code = g16(v, be);
                        if (code < begin || code > end || (k && code <= previous) || g16(v + 2, be) >= capacity) {
                            return false;
                        }
                        previous = code;
                    }
                }
            }
        }
        xx_rt_snprintf(label, sizeof(label), "%s.bin", kinds[i] == 0 ? "font-info" : kinds[i] == 1 ? "texture-header" : kinds[i] == 2 ? "widths" : "character-map");
        if (!emit(f, s, label, offsets[i], kinds[i] == 1 ? 32 : sizes[i], total)) return false;
    }
    for (i = 0; i < sheets; ++i) {
        char label[40];
        xx_rt_snprintf(label, sizeof(label), "sheet-%u.bin", i);
        if (!emit(f, s, label, texture + (uint64_t)i * sheet_size, sheet_size, total)) return false;
    }
    s->size = total;
    return true;
}

void xx_nintendo_bcfnt_init(xx_nintendo_bcfnt *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_BCFNT, "bcfnt");
    }
}
xx_nintendo_bcfnt *xx_nintendo_bcfnt_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_bcfnt *r = (xx_nintendo_bcfnt *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_bcfnt_init(r, d, b);
    return r;
}
void xx_nintendo_bcfnt_destroy(xx_nintendo_bcfnt *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_bcfnt_free(xx_nintendo_bcfnt *r)
{
    if (r) {
        xx_nintendo_bcfnt_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_bcfnt_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_bcfnt_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
