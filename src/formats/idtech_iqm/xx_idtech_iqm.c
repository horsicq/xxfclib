/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/lsalzman/iqm/master/iqm.h
 * IQM2 static meshes with text/mesh/triangle tables and float position/texcoord/normal/tangent or byte-color arrays, up to65536 vertices/triangles and256 meshes.
 * Validates mesh/index/string references, finite values, disjoint buffers and disjoint mesh triangle groups covering all triangles. Exports stored encoded tables/arrays;
 * joints, poses, animation, blend attributes, custom arrays/extensions and rendering unsupported.
 */
#include "xxfclib/formats/idtech_iqm/xx_idtech_iqm.h"
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
typedef struct rg {
    uint64_t at, n;
} rg;
static bool reserve(rg *r, unsigned *count, unsigned maximum, uint64_t at, uint64_t n, uint64_t lower, uint64_t end)
{
    unsigned i;
    if (*count >= maximum || at < lower || !span(at, n, end)) return false;
    for (i = 0; i < *count; ++i)
        if (overlap(at, n, r[i].at, r[i].n)) return false;
    r[*count].at = at;
    r[*count].n = n;
    ++*count;
    return true;
}
static bool zeros(Abstractformat *f, uint64_t at, uint64_t n, xx_pd_struct *pd)
{
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b = NULL;
    bool buffer_result = false;
    if (n) {
        if (capacity > n) capacity = (size_t)n;
        b = (uint8_t *)xx_mem_alloc(capacity);
        if (!b) {
            buffer_result = (false);
            goto buffer_done;
        }
    }
    size_t i;
    while (n) {
        size_t part = n > capacity ? capacity : (size_t)n;
        if (stop(pd) || !pm_read(f, (int64_t)at, b, part)) {
            buffer_result = (false);
            goto buffer_done;
        }
        for (i = 0; i < part; ++i)
            if (b[i]) {
                buffer_result = (false);
                goto buffer_done;
            }
        at += part;
        n -= part;
    }
    {
        buffer_result = (true);
        goto buffer_done;
    }
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[124], m[24], p[20], q[12];
    uint32_t total, text, to, meshes, mo, arrays, verts, ao, tris, tro, adj, i, j, nr = 0;
    uint64_t at, covered = 0;
    bool position = false;
    char label[40];
    rg ranges[256];
    if (!pm_read(f, 0, h, 124) || xx_rt_memcmp(h, "INTERQUAKEMODEL\0", 16) || xx_data_get_u32(h + 16, 4, 0, false) != 2 ||
        (total = xx_data_get_u32(h + 20, 4, 0, false)) > (uint64_t)pm_available(f) || xx_data_get_u32(h + 24, 4, 0, false) || !zeros(f, 68, 40, pd) ||
        xx_data_get_u32(h + 116, 4, 0, false) || xx_data_get_u32(h + 120, 4, 0, false))
        return false;
    text = xx_data_get_u32(h + 28, 4, 0, false);
    to = xx_data_get_u32(h + 32, 4, 0, false);
    meshes = xx_data_get_u32(h + 36, 4, 0, false);
    mo = xx_data_get_u32(h + 40, 4, 0, false);
    arrays = xx_data_get_u32(h + 44, 4, 0, false);
    verts = xx_data_get_u32(h + 48, 4, 0, false);
    ao = xx_data_get_u32(h + 52, 4, 0, false);
    tris = xx_data_get_u32(h + 56, 4, 0, false);
    tro = xx_data_get_u32(h + 60, 4, 0, false);
    adj = xx_data_get_u32(h + 64, 4, 0, false);
    if (!text || text > 1048576 || !meshes || meshes > 256 || !arrays || arrays > 7 || !verts || verts > 65536 || !tris || tris > 65536 || to < 124 || mo < 124 ||
        ao < 124 || tro < 124 || (mo & 3) || (ao & 3) || (tro & 3) || !emit(f, s, "text.bin", to, text, total) ||
        !emit(f, s, "meshes.bin", mo, (uint64_t)meshes * 24, total) || !emit(f, s, "vertex-descriptors.bin", ao, (uint64_t)arrays * 20, total) ||
        !emit(f, s, "triangles.bin", tro, (uint64_t)tris * 12, total))
        return false;
    for (i = 0; i < meshes; ++i) {
        uint32_t first, n, ft, nt;
        if (stop(pd) || !pm_read(f, mo + (int64_t)i * 24, m, 24)) return false;
        at = to + (uint64_t)xx_data_get_u32(m, 4, 0, false);
        if (at >= to + (uint64_t)text || !cstring(f, &at, to + (uint64_t)text, 4096, true, pd)) return false;
        at = to + (uint64_t)xx_data_get_u32(m + 4, 4, 0, false);
        if (at >= to + (uint64_t)text || !cstring(f, &at, to + (uint64_t)text, 4096, true, pd)) return false;
        first = xx_data_get_u32(m + 8, 4, 0, false);
        n = xx_data_get_u32(m + 12, 4, 0, false);
        ft = xx_data_get_u32(m + 16, 4, 0, false);
        nt = xx_data_get_u32(m + 20, 4, 0, false);
        if (!n || !nt || !span(first, n, verts) || !reserve(ranges, &nr, 256, ft, nt, 0, tris)) return false;
        covered += nt;
        for (j = 0; j < nt; ++j) {
            unsigned k;
            if (stop(pd) || !pm_read(f, tro + (int64_t)(ft + j) * 12, q, 12)) return false;
            for (k = 0; k < 3; ++k)
                if (xx_data_get_u32(q + k * 4, 4, 0, false) < first || xx_data_get_u32(q + k * 4, 4, 0, false) - first >= n) return false;
        }
    }
    {
        unsigned used = 0;
        for (i = 0; i < arrays; ++i) {
            uint32_t kind, format, size, offset;
            if (!pm_read(f, ao + (int64_t)i * 20, p, 20)) return false;
            kind = xx_data_get_u32(p, 4, 0, false);
            format = xx_data_get_u32(p + 8, 4, 0, false);
            size = xx_data_get_u32(p + 12, 4, 0, false);
            offset = xx_data_get_u32(p + 16, 4, 0, false);
            if (kind > 6 || kind == 4 || kind == 5 || (used & (1U << kind)) || xx_data_get_u32(p + 4, 4, 0, false) || offset < 124) return false;
            used |= 1U << kind;
            if (kind == 6) {
                if (format != 1 || size != 4) return false;
            } else {
                if (format != 7 ||
                    size != (kind == 1   ? 2U
                             : kind == 3 ? 4U
                                         : 3U) ||
                    (offset & 3) || !span(offset, (uint64_t)verts * size * 4, total) || !floats(f, offset, (uint64_t)verts * size, false, pd))
                    return false;
            }
            xx_rt_snprintf(label, sizeof(label), "vertex-array-%u.bin", kind);
            if (!emit(f, s, label, offset, (uint64_t)verts * size * (format == 7 ? 4 : 1), total)) return false;
            if (!kind) position = true;
        }
    }
    if (adj) {
        if (adj < 124 || (adj & 3) || !emit(f, s, "adjacency.bin", adj, (uint64_t)tris * 12, total)) return false;
        for (i = 0; i < tris; ++i) {
            if (stop(pd) || !pm_read(f, adj + (int64_t)i * 12, q, 12)) return false;
            for (j = 0; j < 3; ++j)
                if (xx_data_get_u32(q + j * 4, 4, 0, false) != UINT32_MAX && xx_data_get_u32(q + j * 4, 4, 0, false) >= tris) return false;
        }
    }
    if (xx_data_get_u32(h + 108, 4, 0, false)) {
        if (xx_data_get_u32(h + 108, 4, 0, false) > 1048576 || xx_data_get_u32(h + 112, 4, 0, false) < 124 ||
            !emit(f, s, "comment.bin", xx_data_get_u32(h + 112, 4, 0, false), xx_data_get_u32(h + 108, 4, 0, false), total))
            return false;
    } else if (xx_data_get_u32(h + 112, 4, 0, false)) return false;
    if (!position || covered != tris) {
        return false;
    }
    s->size = total;
    return true;
}

void xx_idtech_iqm_init(xx_idtech_iqm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_IDTECH_IQM, "iqm");
    }
}
xx_idtech_iqm *xx_idtech_iqm_create(xx_io_device *d, int64_t b)
{
    xx_idtech_iqm *r = (xx_idtech_iqm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_idtech_iqm_init(r, d, b);
    return r;
}
void xx_idtech_iqm_destroy(xx_idtech_iqm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_idtech_iqm_free(xx_idtech_iqm *r)
{
    if (r) {
        xx_idtech_iqm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_idtech_iqm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_idtech_iqm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
