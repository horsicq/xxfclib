/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/3DS/3DSHelper.h
 * 3DS static triangular mesh subset: main/version/edit/mesh-version/object chunks, vertex/face tables, optional UV and local matrix. Full nesting extents, finite values,
 * vertex references and unique required chunks; up to1024 objects. Exports original object chunks. Materials, face subchunks, lights, cameras, animation and rendering
 * unsupported.
 */
#include "xxfclib/formats/autodesk_3ds/xx_autodesk_3ds.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static __inline bool span(uint64_t a, uint64_t n, uint64_t e)
{
    return a <= e && n <= e - a;
}
static __inline bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static __inline bool zero(const uint8_t *b, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (b[i]) return false;
    return true;
}
static __inline bool finite32(const uint8_t *p, bool be)
{
    return (xx_data_get_u32(p, 4, 0, be) & 0x7f800000U) != 0x7f800000U;
}
static __inline bool finite64(const uint8_t *p, bool be)
{
    return (xx_data_get_u64(p, 8, 0, be) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
static __inline bool floats(const uint8_t *b, uint64_t at, uint64_t count, bool be, uint64_t n)
{
    uint64_t i;
    if (!span(at, count * 4, n)) return false;
    for (i = 0; i < count; ++i)
        if (!finite32(b + at + i * 4, be)) return false;
    return true;
}
static __inline bool emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t a, uint64_t n, uint64_t e)
{
    return span(a, n, e) && s->count < 4096 && pm_add(f, s, label, (int64_t)a, (int64_t)n);
}
static __inline bool cstr(const uint8_t *b, uint64_t *at, uint64_t end, uint64_t maximum, bool empty)
{
    uint64_t start = *at;
    while (*at < end && *at - start <= maximum) {
        uint8_t c = b[(*at)++];
        if (!c) return empty || *at > start + 1;
        if (c < 32 || c == 127) return false;
    }
    return false;
}
typedef struct range {
    uint64_t at, n;
} range;
static __inline bool reserve(range *r, unsigned *nr, unsigned max, uint64_t at, uint64_t n, uint64_t lo, uint64_t end)
{
    unsigned i;
    if (*nr >= max || at < lo || !span(at, n, end)) return false;
    for (i = 0; i < *nr; ++i)
        if (n && r[i].n && at < r[i].at + r[i].n && r[i].at < at + n) return false;
    r[*nr].at = at;
    r[*nr].n = n;
    ++*nr;
    return true;
}
static __inline uint32_t crc32_bytes(const uint8_t *b, uint64_t n)
{
    return xx_crc32_calc(0U, b, (size_t)n);
}

static bool mesh3ds(const uint8_t *b, uint64_t at, uint64_t end, xx_pd_struct *pd)
{
    uint32_t nv = 0, nf = 0, uv_count = 0;
    uint64_t vertices = 0, faces = 0;
    bool uv = false, matrix = false;
    while (at < end) {
        uint16_t id;
        uint32_t n;
        uint64_t p, e;
        uint32_t count;
        if (stop(pd) || !span(at, 6, end) || (n = xx_data_get_u32(b + at + 2, 4, 0, false)) < 6 || !span(at, n, end)) return false;
        id = xx_data_get_u16(b + at, 2, 0, false);
        p = at + 6;
        e = at + n;
        switch (id) {
            case 0x4110:
                if (vertices || !span(p, 2, e) || !(nv = xx_data_get_u16(b + p, 2, 0, false)) || e - p != 2 + (uint64_t)nv * 12 ||
                    !floats(b, p + 2, (uint64_t)nv * 3, false, e))
                    return false;
                vertices = p + 2;
                break;
            case 0x4120:
                if (faces || !span(p, 2, e) || !(nf = xx_data_get_u16(b + p, 2, 0, false)) || e - p != 2 + (uint64_t)nf * 8) return false;
                faces = p + 2;
                break;
            case 0x4140:
                if (uv || !span(p, 2, e) || !(count = xx_data_get_u16(b + p, 2, 0, false)) || e - p != 2 + (uint64_t)count * 8 ||
                    !floats(b, p + 2, (uint64_t)count * 2, false, e))
                    return false;
                uv = true;
                uv_count = count;
                break;
            case 0x4160:
                if (matrix || e - p != 48 || !floats(b, p, 12, false, e)) return false;
                matrix = true;
                break;
            default: return false;
        }
        at = e;
    }
    if (!vertices || !faces || (uv && uv_count != nv)) {
        return false;
    }
    for (at = 0; at < nf; ++at) {
        unsigned j;
        for (j = 0; j < 3; ++j)
            if (xx_data_get_u16(b + faces + at * 8 + j * 2, 2, 0, false) >= nv) return false;
    }
    return true;
}

static bool parse_data(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t end, at = 6, edit = 0, edit_end = 0;
    bool version = false, mesh_version = false;
    unsigned objects = 0;
    char label[40];
    if (n < 6 || xx_data_get_u16(b, 2, 0, false) != 0x4d4d || (end = xx_data_get_u32(b + 2, 4, 0, false)) < 6 || end > n) return false;
    while (at < end) {
        uint16_t id;
        uint32_t len;
        if (!span(at, 6, end) || (len = xx_data_get_u32(b + at + 2, 4, 0, false)) < 6 || !span(at, len, end)) return false;
        id = xx_data_get_u16(b + at, 2, 0, false);
        if (id == 2) {
            if (version || len != 10 || xx_data_get_u32(b + at + 6, 4, 0, false) != 3) return false;
            version = true;
        } else if (id == 0x3d3d) {
            if (edit) return false;
            edit = at + 6;
            edit_end = at + len;
        } else return false;
        at += len;
    }
    if (!version || !edit) {
        return false;
    }
    at = edit;
    while (at < edit_end) {
        uint32_t len;
        uint16_t id;
        uint64_t p, e;
        if (stop(pd) || !span(at, 6, edit_end) || (len = xx_data_get_u32(b + at + 2, 4, 0, false)) < 6 || !span(at, len, edit_end)) return false;
        id = xx_data_get_u16(b + at, 2, 0, false);
        p = at + 6;
        e = at + len;
        if (id == 0x3d3e) {
            if (mesh_version || len != 10 || xx_data_get_u32(b + p, 4, 0, false) != 3) return false;
            mesh_version = true;
        } else if (id == 0x4000) {
            if (++objects > 1024 || !cstr(b, &p, e, 256, false) || !span(p, 6, e) || xx_data_get_u16(b + p, 2, 0, false) != 0x4100 ||
                xx_data_get_u32(b + p + 2, 4, 0, false) != e - p || !mesh3ds(b, p + 6, e, pd))
                return false;
            xx_rt_snprintf(label, sizeof(label), "object-%u.3dschunk", objects - 1);
            if (!emit(f, s, label, at, len, end)) return false;
        } else return false;
        at = e;
    }
    if (!mesh_version || !objects) {
        return false;
    }
    s->size = (int64_t)end;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint8_t *b, probe[32];
    bool result;
    if (available < 1 || available > 67108864 || stop(pd)) return false;
    if (available < 2 || !pm_read(f, 0, probe, 2) || xx_rt_memcmp(probe, "\x4d\x4d", 2)) return false;
    b = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!b) return false;
    result = pm_read(f, 0, b, (size_t)available) && parse_data(f, s, b, (uint64_t)available, pd);
    xx_mem_free(b);
    return result;
}

void xx_autodesk_3ds_init(xx_autodesk_3ds *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AUTODESK_3DS, "3ds");
    }
}
xx_autodesk_3ds *xx_autodesk_3ds_create(xx_io_device *d, int64_t b)
{
    xx_autodesk_3ds *r = (xx_autodesk_3ds *)xx_mem_alloc(sizeof(*r));
    if (r) xx_autodesk_3ds_init(r, d, b);
    return r;
}
void xx_autodesk_3ds_destroy(xx_autodesk_3ds *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_autodesk_3ds_free(xx_autodesk_3ds *r)
{
    if (r) {
        xx_autodesk_3ds_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_autodesk_3ds_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_autodesk_3ds_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
