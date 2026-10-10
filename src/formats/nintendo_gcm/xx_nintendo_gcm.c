/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/FileSystemGCWii.cpp
 * GameCube disc images with bounded DOL and FST. Exports DOL, optional apploader and numbered FST files. Directory/name bounds and nested directory ranges checked; Wii
 * encrypted partitions, rendering and execution unsupported.
 */
#include "xxfclib/formats/nintendo_gcm/xx_nintendo_gcm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i) {
        uint64_t a = (uint64_t)(s->items[i].offset - f->base_address), b = (uint64_t)s->items[i].size;
        if (n && b && at < a + b && a < at + n) return false;
    }
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool zname(Abstractformat *f, uint64_t at, uint64_t end)
{
    uint8_t c;
    uint64_t i;
    if (at >= end || end > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < 4096 && at + i < end; ++i) {
        if (!pm_read(f, (int64_t)(at + i), &c, 1)) return false;
        if (!c) return i != 0;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[0x2460], dol[256], e[12];
    uint64_t fst, fs, doloff, dolsize = 256, end, total = (uint64_t)pm_available(f), app;
    uint32_t count, i, stack[65], ends[65], depth = 0;
    char label[40];
    if (!pm_read(f, 0, h, sizeof(h)) || xx_data_get_u32(h + 0x1c, 4, 0, true) != 0xc2339f3d || xx_data_get_u32(h + 0x18, 4, 0, true)) return false;
    doloff = xx_data_get_u32(h + 0x420, 4, 0, true);
    fst = xx_data_get_u32(h + 0x424, 4, 0, true);
    fs = xx_data_get_u32(h + 0x428, 4, 0, true);
    if (doloff < sizeof(h) || fst < sizeof(h) || fs < 12 || fs > 16U * 1024U * 1024U || xx_data_get_u32(h + 0x42c, 4, 0, true) < fs || !span(fst, fs, total) ||
        !pm_read(f, (int64_t)doloff, dol, sizeof(dol)))
        return false;
    for (i = 0; i < 18; ++i) {
        uint64_t at = xx_data_get_u32(dol + i * 4, 4, 0, true), n = xx_data_get_u32(dol + 0x90 + i * 4, 4, 0, true);
        unsigned j;
        if (!n) {
            if (at) return false;
            continue;
        }
        if (at < 256 || !span(doloff + at, n, total)) return false;
        for (j = 0; j < i; ++j) {
            uint64_t a = xx_data_get_u32(dol + j * 4, 4, 0, true), b = xx_data_get_u32(dol + 0x90 + j * 4, 4, 0, true);
            if (b && at < a + b && a < at + n) return false;
        }
        if (at + n > dolsize) dolsize = at + n;
    }
    if (dolsize == 256 || (doloff < fst + fs && fst < doloff + dolsize) || !emit(f, s, "main.dol", doloff, dolsize, total)) return false;
    app = (uint64_t)xx_data_get_u32(h + 0x2454, 4, 0, true) + xx_data_get_u32(h + 0x2458, 4, 0, true) + 32;
    if (app > 32 && (!span(0x2440, app, total) || 0x2440 + app > doloff || 0x2440 + app > fst || !emit(f, s, "apploader.bin", 0x2440, app, total))) return false;
    if (!pm_read(f, (int64_t)fst, e, 12) || xx_data_get_u32(e, 4, 0, true) != 0x1000000 || xx_data_get_u32(e + 4, 4, 0, true)) return false;
    count = xx_data_get_u32(e + 8, 4, 0, true);
    if (!count || count > 4094 || (uint64_t)count * 12 > fs) return false;
    end = fst + fs;
    if (doloff + dolsize > end) end = doloff + dolsize;
    stack[0] = 0;
    ends[0] = count;
    for (i = 1; i < count; ++i) {
        uint32_t a, b, c;
        uint64_t name;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, (int64_t)(fst + i * 12), e, 12)) return false;
        while (depth && i >= ends[depth]) --depth;
        a = xx_data_get_u32(e, 4, 0, true);
        b = xx_data_get_u32(e + 4, 4, 0, true);
        c = xx_data_get_u32(e + 8, 4, 0, true);
        name = fst + (uint64_t)count * 12 + (a & 0xffffff);
        if ((a >> 24) > 1 || !zname(f, name, fst + fs)) return false;
        if (a >> 24) {
            if (b != stack[depth] || c <= i || c > ends[depth] || depth >= 64) return false;
            ++depth;
            stack[depth] = i;
            ends[depth] = c;
        } else {
            if (b < sizeof(h) || !span(b, c, total) || (b < fst + fs && fst < (uint64_t)b + c)) return false;
            xx_rt_snprintf(label, sizeof(label), "file-%u.bin", i);
            if (!emit(f, s, label, b, c, total)) return false;
            if ((uint64_t)b + c > end) end = (uint64_t)b + c;
        }
    }
    s->size = (int64_t)end;
    return true;
}

void xx_nintendo_gcm_init(xx_nintendo_gcm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_GCM, "gcm");
    }
}
xx_nintendo_gcm *xx_nintendo_gcm_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_gcm *r = (xx_nintendo_gcm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_gcm_init(r, d, b);
    return r;
}
void xx_nintendo_gcm_destroy(xx_nintendo_gcm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_gcm_free(xx_nintendo_gcm *r)
{
    if (r) {
        xx_nintendo_gcm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_gcm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_gcm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
