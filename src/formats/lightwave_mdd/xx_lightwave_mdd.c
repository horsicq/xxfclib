/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/blender/blender-addons/main/io_shape_mdd/export_mdd.py
 * Big-endian MDD vertex caches with1-4096 frames,1-65536 points and at most1million frame-point triples. Validates exact complete size, finite strictly increasing
 * nonnegative timestamps and finite XYZ arrays. Exports timestamps and one original position array per frame; deformation playback unsupported. Signatureless
 * detection/search is offset-zero only.
 */
#include "xxfclib/formats/lightwave_mdd/xx_lightwave_mdd.h"
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

static bool parse_data(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t frames, points, i;
    uint64_t end, at;
    char label[40];
    float prev = -1.0f;
    if (n < 8 || !(frames = xx_data_get_u32(b, 4, 0, true)) || frames > 4096 || !(points = xx_data_get_u32(b + 4, 4, 0, true)) || points > 65536 ||
        (uint64_t)frames * points > 1000000)
        return false;
    end = 8 + (uint64_t)frames * 4 + (uint64_t)frames * points * 12;
    if (end != n || !floats(b, 8, frames, true, end)) return false;
    for (i = 0; i < frames; ++i) {
        uint32_t bits = xx_data_get_u32(b + 8 + i * 4, 4, 0, true);
        float now;
        xx_rt_memcpy(&now, &bits, 4);
        if (now < 0 || now <= prev) return false;
        prev = now;
    }
    if (!emit(f, s, "timestamps.f32be", 8, (uint64_t)frames * 4, end)) {
        return false;
    }
    at = 8 + (uint64_t)frames * 4;
    for (i = 0; i < frames; ++i) {
        if (stop(pd) || !floats(b, at, (uint64_t)points * 3, true, end)) return false;
        xx_rt_snprintf(label, sizeof(label), "frame-%u.xyzf32be", i);
        if (!emit(f, s, label, at, (uint64_t)points * 12, end)) return false;
        at += (uint64_t)points * 12;
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
    if (available < 8 || !pm_read(f, 0, probe, 8) || !xx_data_get_u32(probe, 4, 0, true) || xx_data_get_u32(probe, 4, 0, true) > 4096 ||
        !xx_data_get_u32(probe + 4, 4, 0, true) || xx_data_get_u32(probe + 4, 4, 0, true) > 65536 ||
        (uint64_t)xx_data_get_u32(probe, 4, 0, true) * xx_data_get_u32(probe + 4, 4, 0, true) > 1000000 ||
        8 + (uint64_t)xx_data_get_u32(probe, 4, 0, true) * 4 + (uint64_t)xx_data_get_u32(probe, 4, 0, true) * xx_data_get_u32(probe + 4, 4, 0, true) * 12 !=
            (uint64_t)available)
        return false;
    b = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!b) return false;
    result = pm_read(f, 0, b, (size_t)available) && parse_data(f, s, b, (uint64_t)available, pd);
    xx_mem_free(b);
    return result;
}

void xx_lightwave_mdd_init(xx_lightwave_mdd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LIGHTWAVE_MDD, "mdd");
    }
}
xx_lightwave_mdd *xx_lightwave_mdd_create(xx_io_device *d, int64_t b)
{
    xx_lightwave_mdd *r = (xx_lightwave_mdd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lightwave_mdd_init(r, d, b);
    return r;
}
void xx_lightwave_mdd_destroy(xx_lightwave_mdd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lightwave_mdd_free(xx_lightwave_mdd *r)
{
    if (r) {
        xx_lightwave_mdd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lightwave_mdd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lightwave_mdd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
