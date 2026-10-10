/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/DarklightGames/psk_psa_py/master/src/psk_psa_py/psk/writer.py
 * Classic ActorX PSK with ordered PNTS0000/VTXW0000/FACE0000/MATT0000/REFSKELT/RAWWEIGHTS tables, up to65536 points/wedges/faces and256 bones/materials. Checks finite
 * geometry, material/weight/index references and bone hierarchy counts. Exports encoded tables; extended PSK chunks,32-bit face variants, external textures and rendering
 * unsupported.
 */
#include "xxfclib/formats/unreal_psk/xx_unreal_psk.h"
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
typedef struct rg {
    uint64_t at, n;
} rg;
static bool psx_chunk(Abstractformat *f, uint64_t *at, uint64_t total, const char *name, uint32_t stride, uint32_t maximum, uint32_t *count, xx_pd_struct *pd)
{
    uint8_t h[32];
    size_t n = xx_rt_strlen(name);
    if (!take(f, at, total, h, 32, pd) || xx_rt_memcmp(h, name, n) || !xx_rt_memchr(h + n, 0, 20 - n) || xx_data_get_u32(h + 20, 4, 0, false) != 1999801 ||
        xx_data_get_u32(h + 24, 4, 0, false) != stride || (*count = xx_data_get_u32(h + 28, 4, 0, false)) > maximum || !span(*at, (uint64_t)*count * stride, total))
        return false;
    return true;
}
static bool psx_bones(Abstractformat *f, uint64_t at, uint32_t count, xx_pd_struct *pd)
{
    uint8_t b[120];
    uint32_t i, j, children[256] = {0}, declared[256];
    if (!count || count > 256) return false;
    for (i = 0; i < count; ++i) {
        int32_t parent;
        if (stop(pd) || !pm_read(f, (int64_t)(at + (uint64_t)i * 120), b, 120) || !xx_rt_memchr(b, 0, 64) || (declared[i] = xx_data_get_u32(b + 68, 4, 0, false)) > count)
            return false;
        parent = (int32_t)xx_data_get_u32(b + 72, 4, 0, false);
        if (!i) {
            if (parent != 0 && parent != -1) return false;
        } else {
            if (parent < 0 || (uint32_t)parent >= i) return false;
            ++children[parent];
        }
        for (j = 76; j < 120; j += 4)
            if (!finite32(b + j, false)) return false;
    }
    for (i = 0; i < count; ++i) {
        if (children[i] != declared[i]) return false;
    }
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t at = 0, total = (uint64_t)pm_available(f), starts[6];
    uint32_t count[6], i, j, n;
    uint8_t p[120], q[16];
    const char *tags[] = {"PNTS0000", "VTXW0000", "FACE0000", "MATT0000", "REFSKELT", "RAWWEIGHTS"};
    const uint32_t stride[] = {12, 16, 12, 88, 120, 12}, caps[] = {65536, 65536, 65536, 256, 256, 262144};
    char label[40];
    if (!psx_chunk(f, &at, total, "ACTRHEAD", 0, 0, &n, pd)) return false;
    for (i = 0; i < 6; ++i) {
        if (!psx_chunk(f, &at, total, tags[i], stride[i], caps[i], &count[i], pd) || !count[i]) return false;
        starts[i] = at;
        xx_rt_snprintf(label, sizeof(label), "table-%s.bin", tags[i]);
        if (!emit(f, s, label, at, (uint64_t)count[i] * stride[i], total)) return false;
        at += (uint64_t)count[i] * stride[i];
    }
    if (at != total || !floats(f, starts[0], (uint64_t)count[0] * 3, false, pd) || !psx_bones(f, starts[4], count[4], pd)) return false;
    for (i = 0; i < count[1]; ++i)
        if (stop(pd) || !pm_read(f, (int64_t)(starts[1] + i * 16), p, 16) || xx_data_get_u32(p, 4, 0, false) >= count[0] || !finite32(p + 4, false) ||
            !finite32(p + 8, false) || p[12] >= count[3] || p[13] || xx_data_get_u16(p + 14, 2, 0, false))
            return false;
    for (i = 0; i < count[2]; ++i) {
        if (stop(pd) || !pm_read(f, (int64_t)(starts[2] + i * 12), p, 12) || p[6] >= count[3]) return false;
        for (j = 0; j < 3; ++j)
            if (xx_data_get_u16(p + j * 2, 2, 0, false) >= count[1] || !pm_read(f, (int64_t)(starts[1] + xx_data_get_u16(p + j * 2, 2, 0, false) * 16), q, 16) ||
                q[12] != p[6])
                return false;
    }
    for (i = 0; i < count[3]; ++i)
        if (!pm_read(f, (int64_t)(starts[3] + i * 88), p, 88) || !xx_rt_memchr(p, 0, 64)) return false;
    for (i = 0; i < count[5]; ++i) {
        uint32_t weight;
        if (stop(pd) || !pm_read(f, (int64_t)(starts[5] + i * 12), p, 12) || !finite32(p, false) || ((weight = xx_data_get_u32(p, 4, 0, false)) & 0x80000000U) ||
            !weight || weight > 0x3f800000U || xx_data_get_u32(p + 4, 4, 0, false) >= count[0] || xx_data_get_u32(p + 8, 4, 0, false) >= count[4])
            return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_unreal_psk_init(xx_unreal_psk *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UNREAL_PSK, "psk");
    }
}
xx_unreal_psk *xx_unreal_psk_create(xx_io_device *d, int64_t b)
{
    xx_unreal_psk *r = (xx_unreal_psk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unreal_psk_init(r, d, b);
    return r;
}
void xx_unreal_psk_destroy(xx_unreal_psk *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_unreal_psk_free(xx_unreal_psk *r)
{
    if (r) {
        xx_unreal_psk_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_unreal_psk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_unreal_psk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
