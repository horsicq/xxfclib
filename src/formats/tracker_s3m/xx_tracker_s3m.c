/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/s3m_load.c,
 * https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/s3m.h Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_s3m/xx_tracker_s3m.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t at, uint64_t n, uint64_t *measured)
{
    if (s->count >= 4096 || !binary_range(at, n, (uint64_t)pm_available(f)) || at + n > 268435456 || !pm_add(f, s, label, (int64_t)at, (int64_t)n)) return false;
    if (at + n > *measured) {
        *measured = at + n;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p, size_t n)
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

static bool sm_s3m_pattern(Abstractformat *f, uint64_t at, uint32_t n, const uint8_t channels[32], uint32_t samples, xx_pd_struct *pd)
{
    uint8_t b[2];
    uint32_t row = 0;
    binary_cursor c = {f, at, at + n, pd, 0};
    while (row < 64) {
        uint8_t mask;
        if (!binary_get(&c, b, 1)) return false;
        mask = b[0];
        if (!mask) {
            ++row;
            continue;
        }
        if (!(mask & 224) || channels[mask & 31] == 255 || channels[mask & 31] > 15) return false;
        if (mask & 32) {
            if (!binary_get(&c, b, 2) || b[1] > samples || (b[0] < 254 && ((b[0] & 15) > 11 || (b[0] >> 4) > 9))) return false;
        }
        if (mask & 64) {
            if (!binary_get(&c, b, 1) || b[0] > 64) return false;
        }
        if (mask & 128) {
            if (!binary_get(&c, b, 2) || b[0] > 26) return false;
        }
    }
    return c.at == c.end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[96], tables[768], b[80];
    uint32_t orders, samples, patterns, i, n, ptr, flags;
    uint64_t metadata, measured = 0, at, bytes;
    char label[48];
    if (binary_stop(pd) || !pm_read(f, 0, h, 96) || h[28] != 26 || h[29] != 16 || xx_rt_memcmp(h + 44, "SCRM", 4)) return false;
    orders = xx_data_get_u16(h + 32, 2, 0, false);
    samples = xx_data_get_u16(h + 34, 2, 0, false);
    patterns = xx_data_get_u16(h + 36, 2, 0, false);
    if (!orders || orders > 128 || samples > 64 || !patterns || patterns > 128 || (xx_data_get_u16(h + 38, 2, 0, false) & 128) ||
        (xx_data_get_u16(h + 42, 2, 0, false) != 1 && xx_data_get_u16(h + 42, 2, 0, false) != 2) || h[48] > 64 || !h[49] || h[50] < 32 || (h[53] != 0 && h[53] != 252) ||
        xx_data_get_u16(h + 62, 2, 0, false))
        return false;
    for (i = 0; i < 32; ++i)
        if (h[64 + i] != 255 && h[64 + i] > 15) return false;
    metadata = 96 + orders + (uint64_t)(samples + patterns) * 2 + (h[53] == 252 ? 32 : 0);
    if (!binary_range(96, metadata - 96, (uint64_t)pm_available(f)) || !pm_read(f, 96, tables, (size_t)(metadata - 96))) return false;
    n = 0;
    for (i = 0; i < orders; ++i) {
        if (tables[i] < 254) {
            if (tables[i] >= patterns) return false;
            ++n;
        }
    }
    if (!n || !sm_emit(f, s, "s3m-descriptor.bin", 0, metadata, &measured)) return false;
    if (h[53] == 252)
        for (i = 0; i < 32; ++i)
            if (tables[orders + (samples + patterns) * 2 + i] & ~0x2fU) return false;
    for (i = 0; i < samples; ++i) {
        at = (uint64_t)xx_data_get_u16(tables + orders + i * 2, 2, 0, false) * 16;
        if (at < metadata || !binary_range(at, 80, (uint64_t)pm_available(f)) || !pm_read(f, (int64_t)at, b, 80)) return false;
        if (b[0] != 0 && b[0] != 1) return false;
        xx_rt_snprintf(label, sizeof(label), "sample-%u-descriptor.bin", i + 1);
        if (!sm_emit(f, s, label, at, 80, &measured)) return false;
        if (!b[0]) {
            if (xx_data_get_u32(b + 16, 4, 0, false)) return false;
            continue;
        }
        n = xx_data_get_u32(b + 16, 4, 0, false);
        flags = b[31];
        ptr = ((uint32_t)b[13] << 16) | xx_data_get_u16(b + 14, 2, 0, false);
        if (xx_rt_memcmp(b + 76, "SCRS", 4) || n > 16777216 || b[28] > 64 || b[30] || (flags & ~7U) || !xx_data_get_u32(b + 32, 4, 0, false) ||
            !sm_loop(xx_data_get_u32(b + 20, 4, 0, false), xx_data_get_u32(b + 24, 4, 0, false), n, !!(flags & 1)))
            return false;
        bytes = (uint64_t)n * ((flags & 4) ? 2 : 1) * ((flags & 2) ? 2 : 1);
        at = (uint64_t)ptr * 16;
        if (n) {
            if (at < metadata) return false;
            xx_rt_snprintf(label, sizeof(label), "sample-%u-pcm.bin", i + 1);
            if (!sm_emit(f, s, label, at, bytes, &measured)) return false;
        }
    }
    for (i = 0; i < patterns; ++i) {
        at = (uint64_t)xx_data_get_u16(tables + orders + samples * 2 + i * 2, 2, 0, false) * 16;
        if (!at) continue;
        if (binary_stop(pd) || at < metadata || !pm_read(f, (int64_t)at, b, 2) || (n = xx_data_get_u16(b, 2, 0, false)) < 66 ||
            !binary_range(at, n, (uint64_t)pm_available(f)) || !sm_s3m_pattern(f, at + 2, n - 2, h + 64, samples, pd))
            return false;
        xx_rt_snprintf(label, sizeof(label), "pattern-%u.bin", i);
        if (!sm_emit(f, s, label, at, n, &measured)) return false;
    }
    if (!binary_disjoint(s, (uint64_t)f->base_address)) {
        return false;
    }
    s->size = (int64_t)measured;
    return true;
}

void xx_tracker_s3m_init(xx_tracker_s3m *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_TRACKER_S3M, "s3m");
    }
}
xx_tracker_s3m *xx_tracker_s3m_create(xx_io_device *d, int64_t b)
{
    xx_tracker_s3m *r = (xx_tracker_s3m *)xx_mem_alloc(sizeof(*r));
    if (r) xx_tracker_s3m_init(r, d, b);
    return r;
}
void xx_tracker_s3m_destroy(xx_tracker_s3m *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_tracker_s3m_free(xx_tracker_s3m *r)
{
    if (r) {
        xx_tracker_s3m_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_tracker_s3m_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_tracker_s3m_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
