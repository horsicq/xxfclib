/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/nickworonekin/puyotools/master/src/PuyoTools.Core/Textures/Gvr/GvrTextureDecoder.cs
 * Direct GVRT chunk with single-level tiled I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8 orCMPR plane; validates tile-rounded encoded size. Exports encoded texture bytes;
 * GBIX/GCIX, palettes, mipmaps, PRS, pixel conversion and rendering unsupported.
 */
#include "xxfclib/formats/sega_gvr/xx_sega_gvr.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false);
}
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p, bool be)
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
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p, bool *be)
{
    *be = p[0] == 0xfe && p[1] == 0xff;
    return *be || (p[0] == 0xff && p[1] == 0xfe);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[16];
    uint32_t type, w, he, tw, th, bits;
    uint64_t n, total;
    if (!pm_read(f, 0, h, 16) || xx_rt_memcmp(h, "GVRT", 4) || xx_data_get_u16(h + 8, 2, 0, false) || h[10]) return false;
    type = h[11];
    w = xx_data_get_u16(h + 12, 2, 0, true);
    he = xx_data_get_u16(h + 14, 2, 0, true);
    if (!w || !he || w > 8192 || he > 8192) return false;
    if (type == 0) {
        tw = 8;
        th = 8;
        bits = 4;
    } else if (type == 1 || type == 2) {
        tw = 8;
        th = 4;
        bits = 8;
    } else if (type == 3 || type == 4 || type == 5) {
        tw = 4;
        th = 4;
        bits = 16;
    } else if (type == 6) {
        tw = 4;
        th = 4;
        bits = 32;
    } else if (type == 14) {
        tw = 8;
        th = 8;
        bits = 4;
    } else return false;
    n = (uint64_t)((w + tw - 1) / tw) * ((he + th - 1) / th) * tw * th * bits / 8;
    total = 16 + n;
    if (xx_data_get_u32(h + 4, 4, 0, false) != n + 8 || total > (uint64_t)pm_available(f) || (pd && xx_pd_is_stopped(pd))) return false;
    s->size = (int64_t)total;
    return emit(f, s, "texture.bin", 16, n, total);
}

void xx_sega_gvr_init(xx_sega_gvr *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SEGA_GVR, "gvr");
    }
}
xx_sega_gvr *xx_sega_gvr_create(xx_io_device *d, int64_t b)
{
    xx_sega_gvr *r = (xx_sega_gvr *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sega_gvr_init(r, d, b);
    return r;
}
void xx_sega_gvr_destroy(xx_sega_gvr *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sega_gvr_free(xx_sega_gvr *r)
{
    if (r) {
        xx_sega_gvr_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sega_gvr_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sega_gvr_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
