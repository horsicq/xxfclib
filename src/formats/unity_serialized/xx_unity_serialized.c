/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Perfare/AssetStudio/master/AssetStudio/SerializedFile.cs
 * Unity SerializedFile version22 extended headers, little-endian metadata, disabled type trees, up to64 nonscript classes and1024 stored objects. Validates type IDs,
 * unique path IDs, object ranges, metadata end and absent external/script/ref-type tables. Exports object bytes; UnityFS, script objects, type trees, dependency
 * resolution and deserialization unsupported.
 */
#include "xxfclib/formats/unity_serialized/xx_unity_serialized.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint64_t g64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : (uint64_t)xx_data_get_u32(p, 4, 0, false) | ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32);
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
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[48], e[24], p[4];
    uint64_t total, data, at = 48, end, ids[1024], start;
    uint32_t meta, types, objects, i, j, n, type;
    char label[40];
    if (!pm_read(f, 0, h, 48) || xx_data_get_u32(h, 4, 0, true) || xx_data_get_u32(h + 4, 4, 0, true) || xx_data_get_u32(h + 8, 4, 0, true) != 22 ||
        xx_data_get_u32(h + 12, 4, 0, true) || xx_data_get_u32(h + 16, 4, 0, false) || g64(h + 40, true))
        return false;
    meta = xx_data_get_u32(h + 20, 4, 0, true);
    total = g64(h + 24, true);
    data = g64(h + 32, true);
    end = 48U + (uint64_t)meta;
    if (meta < 16 || meta > 16777216 || total > (uint64_t)pm_available(f) || data < end || data > total || (data & 15) || !cstring(f, &at, end, 128, false, pd) ||
        !take(f, &at, end, p, 4, pd) || (int32_t)xx_data_get_u32(p, 4, 0, false) < 0 || !take(f, &at, end, e, 1, pd) || e[0])
        return false;
    if (!take(f, &at, end, p, 4, pd) || !(types = xx_data_get_u32(p, 4, 0, false)) || types > 64) return false;
    for (i = 0; i < types; ++i) {
        if (!take(f, &at, end, e, 23, pd) || (int32_t)xx_data_get_u32(e, 4, 0, false) <= 0 || xx_data_get_u32(e, 4, 0, false) == 114 || e[4] > 1 ||
            xx_data_get_u16(e + 5, 2, 0, false) != 65535)
            return false;
    }
    if (!take(f, &at, end, p, 4, pd) || !(objects = xx_data_get_u32(p, 4, 0, false)) || objects > 1024) return false;
    for (i = 0; i < objects; ++i) {
        uint64_t id;
        at = (at + 3) & ~3ULL;
        if (!take(f, &at, end, e, 24, pd)) return false;
        id = g64(e, false);
        start = g64(e + 8, false);
        n = xx_data_get_u32(e + 16, 4, 0, false);
        type = xx_data_get_u32(e + 20, 4, 0, false);
        if (!id || !n || type >= types || !span(start, n, total - data)) {
            return false;
        }
        for (j = 0; j < i; ++j)
            if (ids[j] == id) return false;
        ids[i] = id;
        xx_rt_snprintf(label, sizeof(label), "object-%u.bin", i);
        if (!emit(f, s, label, data + start, n, total)) return false;
    }
    for (i = 0; i < 3; ++i)
        if (!take(f, &at, end, p, 4, pd) || xx_data_get_u32(p, 4, 0, false)) return false;
    if (!cstring(f, &at, end, 4096, true, pd) || at != end) {
        return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_unity_serialized_init(xx_unity_serialized *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UNITY_SERIALIZED, "assets");
    }
}
xx_unity_serialized *xx_unity_serialized_create(xx_io_device *d, int64_t b)
{
    xx_unity_serialized *r = (xx_unity_serialized *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unity_serialized_init(r, d, b);
    return r;
}
void xx_unity_serialized_destroy(xx_unity_serialized *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_unity_serialized_free(xx_unity_serialized *r)
{
    if (r) {
        xx_unity_serialized_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_unity_serialized_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_unity_serialized_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
