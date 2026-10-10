/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/B3D/B3DImporter.cpp
 * BB3D version1 static model hierarchies, up to1024 nodes/depth32, bounded texture/brush records and finite vertex/triangle streams. Exports encoded texture/brush/mesh
 * streams and validates bindings/indices. Bones/keyframes/animation/unknown chunks, external texture loading and rendering unsupported.
 */
#include "xxfclib/formats/blitz3d_b3d/xx_blitz3d_b3d.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

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
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool take(Abstractformat *f, uint64_t *at, uint64_t end, void *p, size_t n, xx_pd_struct *pd)
{
    if (stop(pd) || !span(*at, n, end) || !pm_read(f, (int64_t)*at, p, n)) return false;
    *at += n;
    return true;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool finite32(const uint8_t *p, bool be)
{
    return (g32(p, be) & 0x7f800000U) != 0x7f800000U;
}
static bool floats(Abstractformat *f, uint64_t at, uint64_t count, bool be, xx_pd_struct *pd)
{
    uint8_t p[4];
    uint64_t i;
    for (i = 0; i < count; ++i)
        if (stop(pd) || !pm_read(f, (int64_t)(at + i * 4), p, 4) || !finite32(p, be)) return false;
    return true;
}
static bool cstring(Abstractformat *f, uint64_t *at, uint64_t end, unsigned maximum, bool empty, xx_pd_struct *pd)
{
    uint8_t c;
    unsigned i;
    for (i = 0; i < maximum; ++i) {
        if (!take(f, at, end, &c, 1, pd)) return false;
        if (!c) return empty || i != 0;
    }
    return false;
}
static bool b3d_chunk(Abstractformat *f, uint64_t *at, uint64_t end, uint8_t tag[4], uint64_t *payload_end, xx_pd_struct *pd)
{
    uint8_t h[8];
    if (!take(f, at, end, h, 8, pd) || !span(*at, xx_data_get_u32(h + 4, 4, 0, false), end)) return false;
    xx_rt_memcpy(tag, h, 4);
    *payload_end = *at + xx_data_get_u32(h + 4, 4, 0, false);
    return true;
}
static bool b3d_node(Abstractformat *f, pm_stream *s, uint64_t at, uint64_t end, uint64_t total, unsigned depth, uint32_t brushes, uint32_t *nodes, xx_pd_struct *pd)
{
    uint8_t tag[4], p[16];
    uint64_t child, start;
    bool mesh = false;
    if (depth > 32 || ++*nodes > 1024 || !cstring(f, &at, end, 4096, true, pd) || !span(at, 40, end) || !floats(f, at, 10, false, pd)) {
        return false;
    }
    at += 40;
    while (at < end) {
        if (!b3d_chunk(f, &at, end, tag, &child, pd)) return false;
        if (!xx_rt_memcmp(tag, "NODE", 4)) {
            if (!b3d_node(f, s, at, child, total, depth + 1, brushes, nodes, pd)) return false;
        } else if (!xx_rt_memcmp(tag, "MESH", 4)) {
            uint32_t verts = 0, tris = 0;
            uint64_t mi = at, part;
            if (mesh || !take(f, &at, child, p, 4, pd) || ((int32_t)xx_data_get_u32(p, 4, 0, false) != -1 && xx_data_get_u32(p, 4, 0, false) >= brushes)) return false;
            mesh = true;
            while (at < child) {
                if (!b3d_chunk(f, &at, child, tag, &part, pd)) return false;
                start = at;
                if (!xx_rt_memcmp(tag, "VRTS", 4)) {
                    uint32_t flags, sets, size, stride;
                    if (verts || !take(f, &at, part, p, 12, pd)) return false;
                    flags = xx_data_get_u32(p, 4, 0, false);
                    sets = xx_data_get_u32(p + 4, 4, 0, false);
                    size = xx_data_get_u32(p + 8, 4, 0, false);
                    if (flags > 3 || sets > 4 || size > 4 || (!!sets != !!size)) {
                        return false;
                    }
                    stride = 12 + ((flags & 1) ? 12 : 0) + ((flags & 2) ? 16 : 0) + sets * size * 4;
                    if ((part - at) % stride || (part - at) / stride > 65536 || !(verts = (uint32_t)((part - at) / stride)) ||
                        !floats(f, at, (part - at) / 4, false, pd) || !emit(f, s, "vertex-stream.bin", start, part - start, total))
                        return false;
                } else if (!xx_rt_memcmp(tag, "TRIS", 4)) {
                    uint32_t i, n;
                    if (!verts || !take(f, &at, part, p, 4, pd) || ((int32_t)xx_data_get_u32(p, 4, 0, false) != -1 && xx_data_get_u32(p, 4, 0, false) >= brushes) ||
                        (part - at) % 12 || (part - at) / 12 > 65536 || !(n = (uint32_t)((part - at) / 12)))
                        return false;
                    for (i = 0; i < n; ++i) {
                        unsigned j;
                        if (!take(f, &at, part, p, 12, pd)) return false;
                        for (j = 0; j < 3; ++j)
                            if (xx_data_get_u32(p + j * 4, 4, 0, false) >= verts) return false;
                    }
                    if (!emit(f, s, "triangle-stream.bin", start, part - start, total)) {
                        return false;
                    }
                    ++tris;
                } else {
                    return false;
                }
                at = part;
            }
            if (!verts || !tris || !span(mi, 4, child)) return false;
        } else return false;
        at = child;
    }
    return at == end;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], tag[4], p[32];
    uint64_t total, at = 12, child, start;
    uint32_t textures = 0, brushes = 0, nodes = 0;
    bool texseen = false, brushseen = false;
    if (!pm_read(f, 0, h, 12) || xx_rt_memcmp(h, "BB3D", 4) || xx_data_get_u32(h + 8, 4, 0, false) != 1) {
        return false;
    }
    total = 8U + (uint64_t)xx_data_get_u32(h + 4, 4, 0, false);
    if (total < 12 || total > (uint64_t)pm_available(f)) return false;
    while (at < total) {
        if (!b3d_chunk(f, &at, total, tag, &child, pd)) return false;
        start = at;
        if (!xx_rt_memcmp(tag, "TEXS", 4)) {
            if (texseen || brushseen || nodes) return false;
            texseen = true;
            while (at < child) {
                if (++textures > 1024 || !cstring(f, &at, child, 4096, false, pd) || !take(f, &at, child, p, 28, pd)) return false;
                {
                    unsigned j;
                    for (j = 8; j < 28; j += 4)
                        if (!finite32(p + j, false)) return false;
                }
            }
            if (child > start && !emit(f, s, "texture-table.bin", start, child - start, total)) return false;
        } else if (!xx_rt_memcmp(tag, "BRUS", 4)) {
            uint32_t refs;
            if (brushseen || nodes || !take(f, &at, child, p, 4, pd) || (refs = xx_data_get_u32(p, 4, 0, false)) > 8) return false;
            brushseen = true;
            while (at < child) {
                uint32_t i;
                if (++brushes > 1024 || !cstring(f, &at, child, 4096, true, pd) || !take(f, &at, child, p, 28, pd)) return false;
                for (i = 0; i < 20; i += 4)
                    if (!finite32(p + i, false)) return false;
                for (i = 0; i < refs; ++i) {
                    if (!take(f, &at, child, p, 4, pd) || ((int32_t)xx_data_get_u32(p, 4, 0, false) != -1 && xx_data_get_u32(p, 4, 0, false) >= textures)) return false;
                }
            }
            if (!emit(f, s, "brush-table.bin", start, child - start, total)) return false;
        } else if (!xx_rt_memcmp(tag, "NODE", 4)) {
            if (!b3d_node(f, s, at, child, total, 0, brushes, &nodes, pd)) return false;
        } else {
            return false;
        }
        at = child;
    }
    if (!nodes || !s->count) {
        return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_blitz3d_b3d_init(xx_blitz3d_b3d *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_BLITZ3D_B3D, "b3d");
    }
}
xx_blitz3d_b3d *xx_blitz3d_b3d_create(xx_io_device *d, int64_t b)
{
    xx_blitz3d_b3d *r = (xx_blitz3d_b3d *)xx_mem_alloc(sizeof(*r));
    if (r) xx_blitz3d_b3d_init(r, d, b);
    return r;
}
void xx_blitz3d_b3d_destroy(xx_blitz3d_b3d *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_blitz3d_b3d_free(xx_blitz3d_b3d *r)
{
    if (r) {
        xx_blitz3d_b3d_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_blitz3d_b3d_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_blitz3d_b3d_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
