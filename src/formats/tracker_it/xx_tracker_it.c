/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/it_load.c,
 * https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/it.h Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_it/xx_tracker_it.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t at, uint64_t n, uint64_t *measured)
{
    if (s->count >= 4096 || !binary_range(at, n, (uint64_t)pm_available(f)) || at + n > 268435456 || !pm_add(f, s, label, (int64_t)at, (int64_t)n)) return false;
    if (at + n > *measured) {
        *measured = at + n;
    }
    return true;
}
static bool sm_zero(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}
static bool sm_loop(uint32_t begin, uint32_t end, uint32_t length, bool enabled)
{
    return !enabled || (begin < end && end <= length);
}

static bool sm_it_pattern(Abstractformat *f, uint64_t at, uint32_t n, uint32_t rows, uint32_t samples, xx_pd_struct *pd)
{
    uint8_t b[2], masks[64] = {0};
    uint32_t row = 0;
    binary_cursor c = {f, at, at + n, pd, 0};
    while (row < rows) {
        uint32_t channel;
        uint8_t mask;
        if (!binary_get(&c, b, 1)) return false;
        if (!b[0]) {
            ++row;
            continue;
        }
        channel = (b[0] & 127U);
        if (!channel || channel > 64) return false;
        --channel;
        if (b[0] & 128) {
            if (!binary_get(&c, b, 1)) return false;
            masks[channel] = b[0];
        }
        mask = masks[channel];
        if ((mask & 1) && (!binary_get(&c, b, 1) || (b[0] > 119 && b[0] < 253))) return false;
        if ((mask & 2) && (!binary_get(&c, b, 1) || b[0] > samples)) return false;
        if ((mask & 4) && (!binary_get(&c, b, 1) || b[0] > 212 || (b[0] >= 125 && b[0] <= 127))) return false;
        if ((mask & 8) && (!binary_get(&c, b, 2) || b[0] > 26)) return false;
    }
    return c.at == c.end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[192], tables[1024], b[80];
    uint32_t orders, samples, patterns, i, n, flags, ptr;
    uint64_t metadata, at, bytes, measured = 0;
    char label[48];
    if (binary_stop(pd) || !pm_read(f, 0, h, 192) || xx_rt_memcmp(h, "IMPM", 4)) return false;
    orders = xx_data_get_u16(h + 32, 2, 0, false);
    samples = xx_data_get_u16(h + 36, 2, 0, false);
    patterns = xx_data_get_u16(h + 38, 2, 0, false);
    flags = xx_data_get_u16(h + 44, 2, 0, false);
    if (!orders || orders > 256 || xx_data_get_u16(h + 34, 2, 0, false) || samples > 64 || !patterns || patterns > 128 || xx_data_get_u16(h + 42, 2, 0, false) < 0x200 ||
        xx_data_get_u16(h + 42, 2, 0, false) > 0x214 || (flags & ~0x3bU) || (xx_data_get_u16(h + 46, 2, 0, false) & ~1U) || h[48] > 128 || h[49] > 128 || !h[50] ||
        h[50] > 31 || h[51] < 32 || h[52] > 128)
        return false;
    for (i = 0; i < 64; ++i)
        if (((h[64 + i] & 127) > 64 && (h[64 + i] & 127) != 100) || h[128 + i] > 64) return false;
    metadata = 192 + orders + (uint64_t)(samples + patterns) * 4;
    if (!binary_range(192, metadata - 192, (uint64_t)pm_available(f)) || !pm_read(f, 192, tables, (size_t)(metadata - 192))) return false;
    n = 0;
    for (i = 0; i < orders; ++i)
        if (tables[i] < 254) {
            if (tables[i] >= patterns) return false;
            ++n;
        }
    if (!n || !sm_emit(f, s, "it-descriptor.bin", 0, metadata, &measured)) return false;
    if (xx_data_get_u16(h + 46, 2, 0, false) & 1) {
        n = xx_data_get_u16(h + 54, 2, 0, false);
        ptr = xx_data_get_u32(h + 56, 4, 0, false);
        if (!n || ptr < metadata || !binary_range(ptr, n, (uint64_t)pm_available(f)) || !pm_read(f, (int64_t)ptr + n - 1, b, 1) || b[0] ||
            !sm_emit(f, s, "message.txt", ptr, n, &measured))
            return false;
    } else if (xx_data_get_u16(h + 54, 2, 0, false) || xx_data_get_u32(h + 56, 4, 0, false)) return false;
    for (i = 0; i < samples; ++i) {
        at = xx_data_get_u32(tables + orders + i * 4, 4, 0, false);
        if (binary_stop(pd) || at < metadata || !binary_range(at, 80, (uint64_t)pm_available(f)) || !pm_read(f, (int64_t)at, b, 80) || xx_rt_memcmp(b, "IMPS", 4) ||
            b[16] || b[17] > 64 || b[19] > 64 || b[46] > 1 || (b[47] & 127) > 64 || b[79] > 3)
            return false;
        flags = b[18];
        n = xx_data_get_u32(b + 48, 4, 0, false);
        ptr = xx_data_get_u32(b + 72, 4, 0, false);
        if ((flags & 8) || n > 16777216 || !!(flags & 1) != !!n || ((flags & 64) && !(flags & 16)) || ((flags & 128) && !(flags & 32)) ||
            !sm_loop(xx_data_get_u32(b + 52, 4, 0, false), xx_data_get_u32(b + 56, 4, 0, false), n, !!(flags & 16)) ||
            !sm_loop(xx_data_get_u32(b + 64, 4, 0, false), xx_data_get_u32(b + 68, 4, 0, false), n, !!(flags & 32)) || (n && !xx_data_get_u32(b + 60, 4, 0, false)))
            return false;
        xx_rt_snprintf(label, sizeof(label), "sample-%u-descriptor.bin", i + 1);
        if (!sm_emit(f, s, label, at, 80, &measured)) return false;
        bytes = (uint64_t)n * ((flags & 2) ? 2 : 1) * ((flags & 4) ? 2 : 1);
        if (n) {
            if (ptr < metadata) return false;
            xx_rt_snprintf(label, sizeof(label), "sample-%u-pcm.bin", i + 1);
            if (!sm_emit(f, s, label, ptr, bytes, &measured)) return false;
        }
    }
    for (i = 0; i < patterns; ++i) {
        uint32_t rows;
        at = xx_data_get_u32(tables + orders + samples * 4 + i * 4, 4, 0, false);
        if (!at) continue;
        if (binary_stop(pd) || at < metadata || !binary_range(at, 8, (uint64_t)pm_available(f)) || !pm_read(f, (int64_t)at, b, 8) ||
            !(rows = xx_data_get_u16(b + 2, 2, 0, false)) || rows > 256 || !sm_zero(b + 4, 4) || !(n = xx_data_get_u16(b, 2, 0, false)) ||
            !binary_range(at + 8, n, (uint64_t)pm_available(f)) || !sm_it_pattern(f, at + 8, n, rows, samples, pd))
            return false;
        xx_rt_snprintf(label, sizeof(label), "pattern-%u.bin", i);
        if (!sm_emit(f, s, label, at, 8 + n, &measured)) return false;
    }
    if (!binary_disjoint(s, (uint64_t)f->base_address)) {
        return false;
    }
    s->size = (int64_t)measured;
    return true;
}

void xx_tracker_it_init(xx_tracker_it *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_IT, "it");
    }
}
xx_tracker_it *xx_tracker_it_create(xx_io_device *d, int64_t b)
{
    xx_tracker_it *r = (xx_tracker_it *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_it_init(r, d, b);
    return r;
}
void xx_tracker_it_destroy(xx_tracker_it *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_it_free(xx_tracker_it *r)
{
    if (r) {
        xx_tracker_it_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_it_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_it_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
